#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <vector>

using namespace std;

const int MAX_KEY_SIZE = 65;
const int M = 100; //B+ tree order

struct Key {
    char str[MAX_KEY_SIZE];

    Key() { memset(str, 0, sizeof(str)); }
    Key(const char* s) {
        memset(str, 0, sizeof(str));
        strncpy(str, s, MAX_KEY_SIZE - 1);
    }

    int cmp(const Key& other) const {
        return strcmp(str, other.str);
    }

    bool operator<(const Key& other) const { return cmp(other) < 0; }
    bool operator==(const Key& other) const { return cmp(other) == 0; }
    bool operator>(const Key& other) const { return cmp(other) > 0; }
    bool operator<=(const Key& other) const { return cmp(other) <= 0; }
    bool operator>=(const Key& other) const { return cmp(other) >= 0; }
};

struct Record {
    Key key;
    int value;

    Record() : value(0) {}
    Record(const Key& k, int v) : key(k), value(v) {}

    bool operator<(const Record& other) const {
        int c = key.cmp(other.key);
        if (c != 0) return c < 0;
        return value < other.value;
    }
    bool operator==(const Record& other) const {
        return key == other.key && value == other.value;
    }
};

class BPlusTree {
private:
    struct Node {
        bool is_leaf;
        int num_keys;
        Key keys[M + 1];
        int children[M + 2]; // child positions for internal nodes, or values for leaf nodes
        int next_leaf; // for leaf nodes

        Node() : is_leaf(true), num_keys(0), next_leaf(-1) {
            memset(children, -1, sizeof(children));
        }
    };

    fstream file;
    string filename;
    int root_pos;
    int free_pos;

    void write_node(int pos, const Node& node) {
        file.seekp(pos);
        file.write(reinterpret_cast<const char*>(&node), sizeof(Node));
        file.flush();
    }

    void read_node(int pos, Node& node) {
        file.seekg(pos);
        file.read(reinterpret_cast<char*>(&node), sizeof(Node));
    }

    int allocate_node() {
        int pos = free_pos;
        free_pos += sizeof(Node);
        return pos;
    }

    void insert_non_full_internal(Node& node, const Key& key, int right_child) {
        int i = node.num_keys - 1;
        while (i >= 0 && key < node.keys[i]) {
            node.keys[i + 1] = node.keys[i];
            node.children[i + 2] = node.children[i + 1];
            i--;
        }
        node.keys[i + 1] = key;
        node.children[i + 2] = right_child;
        node.num_keys++;
    }

    void split_child(int parent_pos, int child_idx, int child_pos) {
        Node parent, child;
        read_node(parent_pos, parent);
        read_node(child_pos, child);

        Node new_node;
        new_node.is_leaf = child.is_leaf;

        int mid = M / 2;
        new_node.num_keys = M - mid;

        if (child.is_leaf) {
            // Copy records to new node
            for (int i = 0; i < new_node.num_keys; i++) {
                new_node.keys[i] = child.keys[mid + i];
                new_node.children[i] = child.children[mid + i];
            }
            new_node.next_leaf = child.next_leaf;
            child.next_leaf = free_pos; // Will be the position of new_node
            child.num_keys = mid;
        } else {
            // Internal node split
            for (int i = 0; i < new_node.num_keys; i++) {
                new_node.keys[i] = child.keys[mid + 1 + i];
                new_node.children[i] = child.children[mid + 1 + i];
            }
            new_node.children[new_node.num_keys] = child.children[M];
            child.num_keys = mid;
        }

        int new_pos = allocate_node();
        write_node(new_pos, new_node);
        write_node(child_pos, child);

        // Insert middle key into parent
        Key split_key = child.is_leaf ? new_node.keys[0] : child.keys[mid];
        insert_non_full_internal(parent, split_key, new_pos);
        write_node(parent_pos, parent);
    }

    void insert_non_full(int node_pos, const Record& record) {
        Node node;
        read_node(node_pos, node);

        if (node.is_leaf) {
            int i = node.num_keys - 1;
            while (i >= 0 && (record.key < node.keys[i] ||
                   (record.key == node.keys[i] && record.value < node.children[i]))) {
                node.keys[i + 1] = node.keys[i];
                node.children[i + 1] = node.children[i];
                i--;
            }
            node.keys[i + 1] = record.key;
            node.children[i + 1] = record.value;
            node.num_keys++;
            write_node(node_pos, node);
        } else {
            int i = node.num_keys - 1;
            while (i >= 0 && record.key < node.keys[i]) {
                i--;
            }
            i++;

            int child_pos = node.children[i];
            Node child;
            read_node(child_pos, child);

            if (child.num_keys == M) {
                split_child(node_pos, i, child_pos);
                read_node(node_pos, node);
                if (record.key >= node.keys[i]) {
                    i++;
                }
            }

            insert_non_full(node.children[i], record);
        }
    }

public:
    BPlusTree(const string& fname) : filename(fname) {
        file.open(filename, ios::in | ios::out | ios::binary);

        if (!file.is_open()) {
            file.clear();
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);

            root_pos = 0;
            free_pos = sizeof(Node);

            Node root;
            root.is_leaf = true;
            root.num_keys = 0;
            write_node(root_pos, root);
        } else {
            file.seekg(0, ios::end);
            free_pos = file.tellg();
            root_pos = 0;
        }
    }

    ~BPlusTree() {
        if (file.is_open()) {
            file.close();
        }
    }

    void insert(const char* key_str, int value) {
        Record record(Key(key_str), value);

        Node root;
        read_node(root_pos, root);

        if (root.num_keys == M) {
            Node new_root;
            new_root.is_leaf = false;
            new_root.num_keys = 0;
            new_root.children[0] = allocate_node();

            // Move old root
            write_node(new_root.children[0], root);

            // Write new root
            write_node(root_pos, new_root);

            // Split the old root (now a child)
            split_child(root_pos, 0, new_root.children[0]);
        }

        insert_non_full(root_pos, record);
    }

    void find(const char* key_str, vector<int>& results) {
        results.clear();
        Key search_key(key_str);

        Node node;
        int pos = root_pos;
        read_node(pos, node);

        // Navigate to leaf
        while (!node.is_leaf) {
            int i = 0;
            while (i < node.num_keys && search_key >= node.keys[i]) {
                i++;
            }
            pos = node.children[i];
            read_node(pos, node);
        }

        // Search in leaves
        while (pos != -1) {
            bool found_any = false;
            for (int i = 0; i < node.num_keys; i++) {
                if (node.keys[i] == search_key) {
                    results.push_back(node.children[i]);
                    found_any = true;
                } else if (node.keys[i] > search_key) {
                    sort(results.begin(), results.end());
                    return;
                }
            }

            // Check next leaf
            if (node.next_leaf == -1) {
                break;
            }

            pos = node.next_leaf;
            read_node(pos, node);

            // If next leaf doesn't start with our key, stop
            if (node.num_keys > 0 && node.keys[0] > search_key) {
                break;
            }
        }

        sort(results.begin(), results.end());
    }

    void remove(const char* key_str, int value) {
        Key target_key(key_str);

        Node node;
        int pos = root_pos;
        read_node(pos, node);

        // Navigate to leaf
        while (!node.is_leaf) {
            int i = 0;
            while (i < node.num_keys && target_key >= node.keys[i]) {
                i++;
            }
            pos = node.children[i];
            read_node(pos, node);
        }

        // Search and delete in leaves
        while (pos != -1) {
            int idx = -1;
            for (int i = 0; i < node.num_keys; i++) {
                if (node.keys[i] == target_key && node.children[i] == value) {
                    idx = i;
                    break;
                } else if (node.keys[i] > target_key) {
                    return; // Not found
                }
            }

            if (idx != -1) {
                // Remove the record
                for (int i = idx; i < node.num_keys - 1; i++) {
                    node.keys[i] = node.keys[i + 1];
                    node.children[i] = node.children[i + 1];
                }
                node.num_keys--;
                write_node(pos, node);
                return;
            }

            // Check next leaf
            if (node.next_leaf == -1) {
                return;
            }

            pos = node.next_leaf;
            read_node(pos, node);

            // If next leaf doesn't have our key, stop
            if (node.num_keys > 0 && node.keys[0] > target_key) {
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
