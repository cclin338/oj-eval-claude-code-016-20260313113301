#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <vector>

using namespace std;

const int MAX_KEY_SIZE = 65;
const int ORDER = 100;

struct Key {
    char str[MAX_KEY_SIZE];

    Key() { memset(str, 0, sizeof(str)); }
    Key(const char* s) {
        memset(str, 0, sizeof(str));
        strncpy(str, s, MAX_KEY_SIZE - 1);
    }

    bool operator<(const Key& other) const {
        return strcmp(str, other.str) < 0;
    }
    bool operator==(const Key& other) const {
        return strcmp(str, other.str) == 0;
    }
    bool operator>(const Key& other) const {
        return strcmp(str, other.str) > 0;
    }
    bool operator<=(const Key& other) const {
        return strcmp(str, other.str) <= 0;
    }
};

struct Record {
    Key key;
    int value;

    Record() : value(0) {}
    Record(const Key& k, int v) : key(k), value(v) {}

    bool operator<(const Record& other) const {
        if (key == other.key) return value < other.value;
        return key < other.key;
    }
    bool operator==(const Record& other) const {
        return key == other.key && value == other.value;
    }
};

struct Node {
    bool is_leaf;
    int num_records;
    int next_leaf; // Only for leaf nodes
    Record records[ORDER + 1];

    Node() : is_leaf(true), num_records(0), next_leaf(-1) {}
};

class BPlusTree {
private:
    fstream file;
    string filename;
    int root_pos;
    int node_count;

    void write_node(int pos, const Node& node) {
        file.seekp(pos * sizeof(Node));
        file.write(reinterpret_cast<const char*>(&node), sizeof(Node));
        file.flush();
    }

    void read_node(int pos, Node& node) {
        file.seekg(pos * sizeof(Node));
        file.read(reinterpret_cast<char*>(&node), sizeof(Node));
    }

    int allocate_node() {
        return node_count++;
    }

    int find_child_index(Node& parent, const Key& key) {
        int i = 0;
        while (i < parent.num_records && key > parent.records[i].key) {
            i++;
        }
        return i;
    }

    void split_node(int node_pos, vector<int>& path, int depth) {
        Node node;
        read_node(node_pos, node);

        int mid = (node.num_records + 1) / 2;

        Node new_node;
        new_node.is_leaf = node.is_leaf;
        new_node.num_records = node.num_records - mid;

        for (int i = 0; i < new_node.num_records; i++) {
            new_node.records[i] = node.records[mid + i];
        }

        if (node.is_leaf) {
            new_node.next_leaf = node.next_leaf;
            node.next_leaf = node_count;
        }

        node.num_records = mid;

        int new_node_pos = allocate_node();
        write_node(node_pos, node);
        write_node(new_node_pos, new_node);

        // Insert the split key into parent
        Record split_record = new_node.records[0];

        if (depth == 0) {
            // Create new root
            Node new_root;
            new_root.is_leaf = false;
            new_root.num_records = 2;
            new_root.records[0].key = node.records[0].key;
            new_root.records[0].value = node_pos;
            new_root.records[1].key = new_node.records[0].key;
            new_root.records[1].value = new_node_pos;

            root_pos = allocate_node();
            write_node(root_pos, new_root);
        } else {
            // Insert into parent
            int parent_pos = path[depth - 1];
            Node parent;
            read_node(parent_pos, parent);

            // Find insertion position
            int i = parent.num_records - 1;
            while (i >= 0 && split_record.key < parent.records[i].key) {
                parent.records[i + 1] = parent.records[i];
                i--;
            }
            parent.records[i + 1].key = split_record.key;
            parent.records[i + 1].value = new_node_pos;
            parent.num_records++;

            write_node(parent_pos, parent);

            if (parent.num_records > ORDER) {
                split_node(parent_pos, path, depth - 1);
            }
        }
    }

    void insert_into_leaf(int leaf_pos, const Record& record, vector<int>& path, int depth) {
        Node leaf;
        read_node(leaf_pos, leaf);

        // Find insertion position
        int i = leaf.num_records - 1;
        while (i >= 0 && record < leaf.records[i]) {
            leaf.records[i + 1] = leaf.records[i];
            i--;
        }
        leaf.records[i + 1] = record;
        leaf.num_records++;

        write_node(leaf_pos, leaf);

        if (leaf.num_records > ORDER) {
            split_node(leaf_pos, path, depth);
        }
    }

    void insert_recursive(int node_pos, const Record& record, vector<int>& path, int depth) {
        path.push_back(node_pos);

        Node node;
        read_node(node_pos, node);

        if (node.is_leaf) {
            insert_into_leaf(node_pos, record, path, depth);
        } else {
            int child_idx = find_child_index(node, record.key);
            int child_pos = node.records[child_idx].value;
            insert_recursive(child_pos, record, path, depth + 1);
        }
    }

public:
    BPlusTree(const string& fname) : filename(fname), root_pos(0), node_count(1) {
        bool file_exists = false;

        ifstream test(filename);
        if (test.good()) {
            file_exists = true;
            test.close();
        }

        if (file_exists) {
            file.open(filename, ios::in | ios::out | ios::binary);
            file.seekg(0, ios::end);
            int file_size = file.tellg();
            node_count = file_size / sizeof(Node);
        } else {
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);

            Node root;
            write_node(root_pos, root);
        }
    }

    ~BPlusTree() {
        if (file.is_open()) {
            file.close();
        }
    }

    void insert(const char* key_str, int value) {
        Record record(Key(key_str), value);
        vector<int> path;
        insert_recursive(root_pos, record, path, 0);
    }

    void find(const char* key_str, vector<int>& results) {
        results.clear();
        Key search_key(key_str);

        // Find the first leaf
        Node node;
        int pos = root_pos;
        read_node(pos, node);

        while (!node.is_leaf) {
            int i = 0;
            while (i < node.num_records && search_key > node.records[i].key) {
                i++;
            }
            if (i == node.num_records) i--;
            pos = node.records[i].value;
            read_node(pos, node);
        }

        // Search through leaves
        while (pos != -1) {
            bool found = false;
            for (int i = 0; i < node.num_records; i++) {
                if (node.records[i].key == search_key) {
                    results.push_back(node.records[i].value);
                    found = true;
                } else if (node.records[i].key > search_key) {
                    break;
                }
            }

            if (!found && results.empty()) {
                // Continue searching in next leaf
                if (node.next_leaf != -1) {
                    pos = node.next_leaf;
                    read_node(pos, node);
                } else {
                    break;
                }
            } else if (found) {
                // Check next leaf for more matches
                if (node.next_leaf != -1) {
                    pos = node.next_leaf;
                    read_node(pos, node);
                    if (node.num_records > 0 && node.records[0].key == search_key) {
                        continue;
                    }
                }
                break;
            } else {
                break;
            }
        }

        sort(results.begin(), results.end());
    }

    void remove(const char* key_str, int value) {
        Key target_key(key_str);
        Record target(target_key, value);

        // Find the leaf
        Node node;
        int pos = root_pos;
        read_node(pos, node);

        while (!node.is_leaf) {
            int i = 0;
            while (i < node.num_records && target_key > node.records[i].key) {
                i++;
            }
            if (i == node.num_records) i--;
            pos = node.records[i].value;
            read_node(pos, node);
        }

        // Search for the record in leaves
        while (pos != -1) {
            int idx = -1;
            for (int i = 0; i < node.num_records; i++) {
                if (node.records[i] == target) {
                    idx = i;
                    break;
                }
            }

            if (idx != -1) {
                // Remove the record
                for (int i = idx; i < node.num_records - 1; i++) {
                    node.records[i] = node.records[i + 1];
                }
                node.num_records--;
                write_node(pos, node);
                return;
            }

            // Check if we should continue to next leaf
            if (node.next_leaf != -1) {
                Node next;
                read_node(node.next_leaf, next);
                if (next.num_records > 0 && next.records[0].key == target_key) {
                    pos = node.next_leaf;
                    node = next;
                } else {
                    return;
                }
            } else {
                return;
            }
        }
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int n;
    cin >> n;

    BPlusTree tree("data.db");

    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;

        if (cmd == "insert") {
            string index;
            int value;
            cin >> index >> value;
            tree.insert(index.c_str(), value);
        } else if (cmd == "delete") {
            string index;
            int value;
            cin >> index >> value;
            tree.remove(index.c_str(), value);
        } else if (cmd == "find") {
            string index;
            cin >> index;

            vector<int> results;
            tree.find(index.c_str(), results);

            if (results.empty()) {
                cout << "null\n";
            } else {
                for (size_t j = 0; j < results.size(); j++) {
                    if (j > 0) cout << " ";
                    cout << results[j];
                }
                cout << "\n";
            }
        }
    }

    return 0;
}
