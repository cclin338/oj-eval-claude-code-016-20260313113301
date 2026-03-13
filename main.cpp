#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <vector>

using namespace std;

const int MAX_KEY_SIZE = 65;
const int M = 100; // B+ tree order
const int MIN_KEYS = M / 2;

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

class BPlusTree {
private:
    struct Node {
        bool is_leaf;
        int num_keys;
        Key keys[M];
        int children[M + 1]; // file positions or values
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

    void split_child(Node& parent, int index, Node& child, int child_pos) {
        Node new_node;
        new_node.is_leaf = child.is_leaf;

        int mid = M / 2;
        new_node.num_keys = M - mid;

        for (int i = 0; i < new_node.num_keys; i++) {
            new_node.keys[i] = child.keys[mid + i];
            if (!child.is_leaf) {
                new_node.children[i] = child.children[mid + i];
            } else {
                new_node.children[i] = child.children[mid + i];
            }
        }

        if (!child.is_leaf) {
            new_node.children[new_node.num_keys] = child.children[M];
        }

        if (child.is_leaf) {
            new_node.next_leaf = child.next_leaf;
            child.next_leaf = free_pos;
        }

        child.num_keys = mid;

        int new_pos = allocate_node();
        write_node(new_pos, new_node);
        write_node(child_pos, child);

        for (int i = parent.num_keys; i > index; i--) {
            parent.keys[i] = parent.keys[i - 1];
            parent.children[i + 1] = parent.children[i];
        }

        if (child.is_leaf) {
            parent.keys[index] = new_node.keys[0];
        } else {
            parent.keys[index] = new_node.keys[0];
        }
        parent.children[index + 1] = new_pos;
        parent.num_keys++;
    }

    void insert_non_full(int node_pos, const Record& record) {
        Node node;
        read_node(node_pos, node);

        int i = node.num_keys - 1;

        if (node.is_leaf) {
            while (i >= 0 && record < Record(node.keys[i], node.children[i])) {
                node.keys[i + 1] = node.keys[i];
                node.children[i + 1] = node.children[i];
                i--;
            }

            node.keys[i + 1] = record.key;
            node.children[i + 1] = record.value;
            node.num_keys++;
            write_node(node_pos, node);
        } else {
            while (i >= 0 && record.key < node.keys[i]) {
                i--;
            }
            i++;

            int child_pos = node.children[i];
            Node child;
            read_node(child_pos, child);

            if (child.num_keys == M) {
                split_child(node, i, child, child_pos);
                write_node(node_pos, node);

                read_node(node_pos, node);
                if (node.keys[i] < record.key || (node.keys[i] == record.key)) {
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
            new_root.children[0] = root_pos;

            int old_root_pos = allocate_node();
            write_node(old_root_pos, root);

            new_root.children[0] = old_root_pos;
            root_pos = 0;
            write_node(root_pos, new_root);

            split_child(new_root, 0, root, old_root_pos);
            write_node(root_pos, new_root);

            insert_non_full(root_pos, record);
        } else {
            insert_non_full(root_pos, record);
        }
    }

    void find(const char* key_str, vector<int>& results) {
        results.clear();
        Key search_key(key_str);

        Node node;
        int pos = root_pos;
        read_node(pos, node);

        while (!node.is_leaf) {
            int i = 0;
            while (i < node.num_keys && search_key < node.keys[i]) {
                i++;
            }
            pos = node.children[i];
            read_node(pos, node);
        }

        bool found = false;
        while (pos != -1) {
            for (int i = 0; i < node.num_keys; i++) {
                if (node.keys[i] == search_key) {
                    results.push_back(node.children[i]);
                    found = true;
                } else if (found) {
                    sort(results.begin(), results.end());
                    return;
                }
            }

            if (found && (node.next_leaf == -1)) {
                break;
            }
            if (!found && node.next_leaf == -1) {
                break;
            }

            pos = node.next_leaf;
            if (pos != -1) {
                read_node(pos, node);
            }
        }

        sort(results.begin(), results.end());
    }

    void remove(const char* key_str, int value) {
        // Simple implementation: rebuild without the removed element
        // For a more efficient version, implement proper B+ tree deletion
        Key remove_key(key_str);

        vector<Record> all_records;

        Node node;
        int pos = root_pos;
        read_node(pos, node);

        while (!node.is_leaf) {
            pos = node.children[0];
            read_node(pos, node);
        }

        while (pos != -1) {
            for (int i = 0; i < node.num_keys; i++) {
                Record r(node.keys[i], node.children[i]);
                if (!(r.key == remove_key && r.value == value)) {
                    all_records.push_back(r);
                }
            }
            pos = node.next_leaf;
            if (pos != -1) {
                read_node(pos, node);
            }
        }

        file.close();
        file.open(filename, ios::out | ios::binary | ios::trunc);
        file.close();
        file.open(filename, ios::in | ios::out | ios::binary);

        root_pos = 0;
        free_pos = sizeof(Node);
        Node root;
        root.is_leaf = true;
        root.num_keys = 0;
        write_node(root_pos, root);

        for (const auto& r : all_records) {
            insert(r.key.str, r.value);
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
