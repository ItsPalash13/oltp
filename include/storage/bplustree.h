#ifndef BPLUSTREE_H
#define BPLUSTREE_H

#include "storage/page/page.h"
#include "executor/scan_position.h"
#include <cstdint>
#include <optional>
#include <utility>
#include <string>
#include <memory>
#include <vector>

class CatalogManager;
class BufferPoolManager;
class StorageManager;
struct Transaction;

/**
 * BPlusTree - clustered B+ tree with data in leaf nodes.
 * One primary index only. Uses root_page_id from catalog (page 0),
 * buffer pool for pages 2+, and page 0 free list for allocation.
 */
class BPlusTree {
public:
    BPlusTree(CatalogManager& catalog, BufferPoolManager& bp, StorageManager& storage,
              const std::string& db_path, const std::string& table_name, uint32_t table_id);

    // Returns (page_id, slot_index) of first leaf slot where key >= search_key; nullopt if no such key.
    std::optional<std::pair<uint32_t, uint16_t>> search(const uint8_t* key, uint16_t key_len);

    // Returns (page_id, slot_index) of the leftmost (smallest key) entry for full index scan; nullopt if tree is empty.
    std::optional<std::pair<uint32_t, uint16_t>> find_leftmost();

    // Returns true if a non-deleted record with this key exists
    bool key_exists(const uint8_t* key, uint16_t key_len);

    // Stateless: return next (key, row) and updated position from (page_id, slot_index). nullopt when no more.
    std::optional<std::pair<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>, ScanPosition>> next_entry_from(uint32_t page_id, uint16_t slot_index);

    void insert(const uint8_t* key, uint16_t key_len, const uint8_t* row, uint16_t row_len, Transaction& txn);

    // Soft delete all records with the given key; purge at 15% dead_bytes; if page empty, unlink and add to free list.
    // Returns the number of records actually deleted (handles duplicate keys).
    int remove(const uint8_t* key, uint16_t key_len);

    // Expose buffer pool and storage for cursors (e.g. IndexCursor needs to fetch pages).
    BufferPoolManager& get_buffer_pool() { return bp_; }
    StorageManager& get_storage() { return storage_; }
    const std::string& get_db_path() const { return db_path_; }
    const std::string& get_table_name() const { return table_name_; }
    CatalogManager& get_catalog() { return catalog_; }

private:
    CatalogManager& catalog_;
    BufferPoolManager& bp_;
    StorageManager& storage_;
    std::string db_path_;
    std::string table_name_;
    uint32_t table_id_;

    uint8_t* get_page0();
    void mark_page0_dirty();
    uint32_t allocate_page();
    void split_leaf(uint32_t leaf_page_id, uint32_t parent_page_id, int slot_in_parent);
    void split_internal(uint32_t internal_page_id, uint32_t parent_page_id, int slot_in_parent);
    void unlink_empty_leaf_and_add_to_freelist(uint32_t leaf_page_id, uint32_t prev_id, uint32_t next_id, uint32_t parent_page_id);
};

#endif // BPLUSTREE_H
