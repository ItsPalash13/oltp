#include <iostream>
#include <fstream>
#include <cstring>

using namespace std;

constexpr size_t PAGE_SIZE = 16384;
constexpr size_t VALUE_SIZE = 255;

// ---------------- Page Structure ----------------
struct Page {
    uint8_t prev;                       // 1 byte
    uint8_t next;                       // 1 byte
    char value[VALUE_SIZE];             // 255 bytes
    char padding[PAGE_SIZE - 2 - VALUE_SIZE];
};

// ---------------- Create / Write Page ----------------
void write_page(
    const string& filename,
    uint32_t page_id,
    int prev = -1,
    int next = -1,
    const string& value = ""
) {
    fstream file(filename, ios::in | ios::out | ios::binary);

    if (!file.is_open()) {
        file.open(filename, ios::out | ios::binary);
        file.close();
        file.open(filename, ios::in | ios::out | ios::binary);
    }

    Page page{};
    memset(&page, 0, sizeof(Page));

    if (prev >= 0) page.prev = static_cast<uint8_t>(prev);
    if (next >= 0) page.next = static_cast<uint8_t>(next);

    if (!value.empty()) {
        strncpy(page.value, value.c_str(), VALUE_SIZE - 1);
    }

    size_t offset = page_id * PAGE_SIZE;
    file.seekp(offset);
    file.write(reinterpret_cast<char*>(&page), sizeof(Page));
    file.close();
}

// ---------------- Read / View Page ----------------
void read_page(const string& filename, uint32_t page_id) {
    ifstream file(filename, ios::binary);
    if (!file.is_open()) {
        cout << "File not found\n";
        return;
    }

    Page page{};
    size_t offset = page_id * PAGE_SIZE;

    file.seekg(offset);
    file.read(reinterpret_cast<char*>(&page), sizeof(Page));
    file.close();

    cout << "Page ID: " << page_id << "\n";
    cout << "Prev: " << static_cast<int>(page.prev) << "\n";
    cout << "Next: " << static_cast<int>(page.next) << "\n";
    cout << "Value: " << page.value << "\n";
    cout << "---------------------------\n";
}

// ---------------- Update Prev / Next ----------------
void update_links(
    const string& filename,
    uint32_t page_id,
    int prev = -1,
    int next = -1
) {
    fstream file(filename, ios::in | ios::out | ios::binary);
    if (!file.is_open()) {
        cout << "File not found\n";
        return;
    }

    Page page{};
    size_t offset = page_id * PAGE_SIZE;

    file.seekg(offset);
    file.read(reinterpret_cast<char*>(&page), sizeof(Page));

    if (prev >= 0) page.prev = static_cast<uint8_t>(prev);
    if (next >= 0) page.next = static_cast<uint8_t>(next);

    file.seekp(offset);
    file.write(reinterpret_cast<char*>(&page), sizeof(Page));
    file.close();
}

// ---------------- Update VALUE ONLY ----------------
void update_value(
    const string& filename,
    uint32_t page_id,
    const string& new_value
) {
    fstream file(filename, ios::in | ios::out | ios::binary);
    if (!file.is_open()) {
        cout << "File not found\n";
        return;
    }

    Page page{};
    size_t offset = page_id * PAGE_SIZE;

    file.seekg(offset);
    file.read(reinterpret_cast<char*>(&page), sizeof(Page));

    memset(page.value, 0, VALUE_SIZE);
    strncpy(page.value, new_value.c_str(), VALUE_SIZE - 1);

    file.seekp(offset);
    file.write(reinterpret_cast<char*>(&page), sizeof(Page));
    file.close();
}

// ---------------- Demo ----------------
int main() {
    const string filename = "demo.ibd";

    // Create pages
    write_page(filename, 0, -1, 1, "ROOT PAGE");
    write_page(filename, 1, 0, 2, "MIDDLE PAGE");
    write_page(filename, 2, 1, -1, "LEAF PAGE");

    // Read pages
    read_page(filename, 0);
    read_page(filename, 1);
    read_page(filename, 2);

    // Update links only
    update_links(filename, 1, 9, 8);
    read_page(filename, 1);

    // Update value only
    update_value(filename, 1, "UPDATED VALUE");
    read_page(filename, 1);

    return 0;
}
