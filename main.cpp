#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <vector>
#include <map>

using namespace std;

const int MAX_KEY_SIZE = 65;

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
    bool operator<=(const Key& other) const {
        return strcmp(str, other.str) <= 0;
    }
    bool operator>(const Key& other) const {
        return strcmp(str, other.str) > 0;
    }
};

class BPlusTree {
private:
    struct Record {
        Key key;
        int value;

        Record() : value(0) {}
        Record(const Key& k, int v) : key(k), value(v) {}

        bool operator<(const Record& other) const {
            if (key == other.key) return value < other.value;
            return key < other.key;
        }
    };

    static const int ORDER = 80;
    static const int MIN_KEYS = ORDER / 2;

    struct Node {
        bool is_leaf;
        int num_keys;
        int parent;
        int next_leaf;
        Record records[ORDER + 1]; // for leaf nodes
        Key keys[ORDER + 1];       // for internal nodes
        int children[ORDER + 2];   // for internal nodes

        Node() : is_leaf(true), num_keys(0), parent(-1), next_leaf(-1) {
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

    int find_leaf(const Key& key) {
        Node node;
        int pos = root_pos;
        read_node(pos, node);

        while (!node.is_leaf) {
            int i = 0;
            while (i < node.num_keys && key > node.keys[i]) {
                i++;
            }
            pos = node.children[i];
            read_node(pos, node);
        }
        return pos;
    }

    void insert_into_leaf(int leaf_pos, const Record& record) {
        Node leaf;
        read_node(leaf_pos, leaf);

        int i = leaf.num_keys - 1;
        while (i >= 0 && record < leaf.records[i]) {
            leaf.records[i + 1] = leaf.records[i];
            i--;
        }
        leaf.records[i + 1] = record;
        leaf.num_keys++;

        write_node(leaf_pos, leaf);
    }

    void split_leaf(int leaf_pos) {
        Node leaf;
        read_node(leaf_pos, leaf);

        Node new_leaf;
        new_leaf.is_leaf = true;
        new_leaf.next_leaf = leaf.next_leaf;

        int mid = (ORDER + 1) / 2;
        new_leaf.num_keys = leaf.num_keys - mid;

        for (int i = 0; i < new_leaf.num_keys; i++) {
            new_leaf.records[i] = leaf.records[mid + i];
        }

        leaf.num_keys = mid;

        int new_leaf_pos = allocate_node();
        leaf.next_leaf = new_leaf_pos;

        write_node(leaf_pos, leaf);
        write_node(new_leaf_pos, new_leaf);

        insert_into_parent(leaf_pos, new_leaf.records[0].key, new_leaf_pos);
    }

    void insert_into_parent(int left_pos, const Key& key, int right_pos) {
        Node left;
        read_node(left_pos, left);

        if (left.parent == -1) {
            Node new_root;
            new_root.is_leaf = false;
            new_root.num_keys = 1;
            new_root.keys[0] = key;
            new_root.children[0] = left_pos;
            new_root.children[1] = right_pos;

            int new_root_pos = allocate_node();
            write_node(new_root_pos, new_root);

            left.parent = new_root_pos;
            write_node(left_pos, left);

            Node right;
            read_node(right_pos, right);
            right.parent = new_root_pos;
            write_node(right_pos, right);

            // Move old root to new position and make new root at position 0
            if (root_pos == 0) {
                Node old_root;
                read_node(root_pos, old_root);
                int old_root_new_pos = allocate_node();
                write_node(old_root_new_pos, old_root);

                // Update children's parent pointer
                if (!old_root.is_leaf) {
                    for (int i = 0; i <= old_root.num_keys; i++) {
                        Node child;
                        read_node(old_root.children[i], child);
                        child.parent = old_root_new_pos;
                        write_node(old_root.children[i], child);
                    }
                }

                new_root.children[0] = old_root_new_pos;
                write_node(root_pos, new_root);

                left.parent = root_pos;
                write_node(left_pos, left);

                right.parent = root_pos;
                write_node(right_pos, right);
            } else {
                root_pos = new_root_pos;
            }
            return;
        }

        int parent_pos = left.parent;
        Node parent;
        read_node(parent_pos, parent);

        if (parent.num_keys < ORDER) {
            insert_into_internal(parent_pos, key, right_pos);
        } else {
            insert_into_internal_and_split(parent_pos, key, right_pos);
        }
    }

    void insert_into_internal(int node_pos, const Key& key, int right_child) {
        Node node;
        read_node(node_pos, node);

        int i = node.num_keys - 1;
        while (i >= 0 && key < node.keys[i]) {
            node.keys[i + 1] = node.keys[i];
            node.children[i + 2] = node.children[i + 1];
            i--;
        }

        node.keys[i + 1] = key;
        node.children[i + 2] = right_child;
        node.num_keys++;

        write_node(node_pos, node);

        Node right;
        read_node(right_child, right);
        right.parent = node_pos;
        write_node(right_child, right);
    }

    void insert_into_internal_and_split(int node_pos, const Key& key, int right_child) {
        Node node;
        read_node(node_pos, node);

        // Temporary arrays
        Key temp_keys[ORDER + 2];
        int temp_children[ORDER + 3];

        int i = 0, j = 0;
        while (j < node.num_keys) {
            if (i == j && key < node.keys[j]) {
                temp_keys[i] = key;
                temp_children[i + 1] = right_child;
                i++;
            } else {
                temp_keys[i] = node.keys[j];
                temp_children[i] = node.children[j];
                i++;
                j++;
            }
        }

        if (i == j) {
            temp_keys[i] = key;
            temp_children[i] = node.children[j];
            temp_children[i + 1] = right_child;
        } else {
            temp_children[i] = node.children[j];
        }

        int split = (ORDER + 1) / 2;

        node.num_keys = split;
        for (int k = 0; k < split; k++) {
            node.keys[k] = temp_keys[k];
            node.children[k] = temp_children[k];
        }
        node.children[split] = temp_children[split];

        Key split_key = temp_keys[split];

        Node new_node;
        new_node.is_leaf = false;
        new_node.num_keys = ORDER + 1 - split - 1;
        for (int k = 0; k < new_node.num_keys; k++) {
            new_node.keys[k] = temp_keys[split + 1 + k];
            new_node.children[k] = temp_children[split + 1 + k];
        }
        new_node.children[new_node.num_keys] = temp_children[ORDER + 1];

        int new_node_pos = allocate_node();

        write_node(node_pos, node);
        write_node(new_node_pos, new_node);

        // Update children's parent pointers
        for (int k = 0; k <= new_node.num_keys; k++) {
            Node child;
            read_node(new_node.children[k], child);
            child.parent = new_node_pos;
            write_node(new_node.children[k], child);
        }

        insert_into_parent(node_pos, split_key, new_node_pos);
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

        int leaf_pos = find_leaf(record.key);
        Node leaf;
        read_node(leaf_pos, leaf);

        if (leaf.num_keys < ORDER) {
            insert_into_leaf(leaf_pos, record);
        } else {
            insert_into_leaf(leaf_pos, record);
            split_leaf(leaf_pos);
        }
    }

    void find(const char* key_str, vector<int>& results) {
        results.clear();
        Key search_key(key_str);

        int leaf_pos = find_leaf(search_key);
        Node leaf;
        read_node(leaf_pos, leaf);

        // Search in current and subsequent leaves
        while (leaf_pos != -1) {
            bool found_in_node = false;
            for (int i = 0; i < leaf.num_keys; i++) {
                if (leaf.records[i].key == search_key) {
                    results.push_back(leaf.records[i].value);
                    found_in_node = true;
                } else if (found_in_node || leaf.records[i].key > search_key) {
                    // We've passed all matching keys
                    sort(results.begin(), results.end());
                    return;
                }
            }

            // Continue to next leaf if we haven't found all keys
            if (leaf.next_leaf != -1) {
                Node next_leaf;
                read_node(leaf.next_leaf, next_leaf);
                if (next_leaf.num_keys > 0 && next_leaf.records[0].key == search_key) {
                    leaf_pos = leaf.next_leaf;
                    leaf = next_leaf;
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        sort(results.begin(), results.end());
    }

    void remove(const char* key_str, int value) {
        Record target(Key(key_str), value);

        int leaf_pos = find_leaf(target.key);
        Node leaf;
        read_node(leaf_pos, leaf);

        // Find and remove the record
        int idx = -1;
        for (int i = 0; i < leaf.num_keys; i++) {
            if (leaf.records[i].key == target.key && leaf.records[i].value == target.value) {
                idx = i;
                break;
            }
        }

        if (idx == -1) {
            // Record not found in this leaf, check next leaves
            while (leaf.next_leaf != -1) {
                leaf_pos = leaf.next_leaf;
                read_node(leaf_pos, leaf);

                if (leaf.num_keys > 0 && leaf.records[0].key == target.key) {
                    for (int i = 0; i < leaf.num_keys; i++) {
                        if (leaf.records[i].key == target.key && leaf.records[i].value == target.value) {
                            idx = i;
                            break;
                        }
                    }
                    if (idx != -1) break;
                } else {
                    return; // Not found
                }
            }

            if (idx == -1) return; // Not found
        }

        // Remove the record
        for (int i = idx; i < leaf.num_keys - 1; i++) {
            leaf.records[i] = leaf.records[i + 1];
        }
        leaf.num_keys--;
        write_node(leaf_pos, leaf);
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
