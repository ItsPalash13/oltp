#include "storage/bplustree.h"
#include "storage/bufferpool_manager.h"
#include "storage/catalog_manager.h"
#include "storage/storage_manager.h"
#include "storage/page/record_layout.h"
#include "storage/page/page.h"
#include "transaction/transaction.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
BPlusTree::BPlusTree(CatalogManager& catalog, BufferPoolManager& bp, StorageManager& storage,
                     const std::string& db_path, const std::string& table_name, uint32_t table_id)
    : catalog_(catalog), bp_(bp), storage_(storage), db_path_(db_path), table_name_(table_name), table_id_(table_id) {}

uint8_t* BPlusTree::get_page0() {
    return catalog_.get_page0(db_path_, table_name_);
}

void BPlusTree::mark_page0_dirty() {
    catalog_.mark_page0_dirty(db_path_, table_name_);
}

uint32_t BPlusTree::allocate_page() {
    uint8_t* page0 = get_page0();
    uint32_t page_id;
    if (pop_free_page(page0, &page_id)) {
        mark_page0_dirty();
        return page_id;
    }
    uint32_t new_id = storage_.get_page_count();
    uint8_t buf[PAGE_SIZE];
    std::memset(buf, 0, PAGE_SIZE);
    storage_.write_page(new_id, buf);
    return new_id;
}

std::optional<std::pair<uint32_t, uint16_t>> BPlusTree::search(const uint8_t* key, uint16_t key_len) {
    uint8_t* page0 = get_page0();
    uint32_t root_id = get_root_page_id(page0);
    if (root_id == ROOT_PAGE_ID_INVALID) {
        return std::nullopt;
    }
    uint8_t* page = bp_.fetch(table_id_,root_id);
    const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
    uint16_t level = h->level;
    uint32_t current_id = root_id;
    bp_.unpin(table_id_,root_id);

    while (level != static_cast<uint16_t>(PageLevel::PAGE_LEAF)) {
        page = bp_.fetch(table_id_,current_id);
        const PageHeader* h_int = reinterpret_cast<const PageHeader*>(page);
        int slot_idx = binary_search_slots_internal(page, key, key_len);
        uint32_t child_id;
        // slot_idx = first slot where slot_key >= search_key
        // If slot_idx >= cell_count: key > all separators, go to rightmost
        // Else: key <= slot[slot_idx].key, go to slot[slot_idx].child
        if (slot_idx >= static_cast<int>(h_int->cell_count)) {
            child_id = get_internal_rightmost(page);
        } else {
            child_id = get_internal_child_at_slot(page, static_cast<uint16_t>(slot_idx));
        }
        bp_.unpin(table_id_,current_id);
        current_id = child_id;
        page = bp_.fetch(table_id_,current_id);
        h = reinterpret_cast<const PageHeader*>(page);
        level = h->level;
        bp_.unpin(table_id_,current_id);
    }

    for (;;) {
        page = bp_.fetch(table_id_,current_id);
        h = reinterpret_cast<const PageHeader*>(page);
        int slot_idx = binary_search_slots(page, key, key_len);
        if (slot_idx < static_cast<int>(h->cell_count)) {
            bp_.unpin(table_id_,current_id);
            return std::make_pair(current_id, static_cast<uint16_t>(slot_idx));
        }
        uint32_t next_id = get_leaf_next_page_id(page);
        bp_.unpin(table_id_,current_id);
        if (next_id == 0xFFFFFFFFu) {
            return std::nullopt;
        }
        current_id = next_id;
    }
}

std::optional<std::pair<uint32_t, uint16_t>> BPlusTree::find_leftmost() {
    uint8_t* page0 = get_page0();
    uint32_t root_id = get_root_page_id(page0);
    if (root_id == ROOT_PAGE_ID_INVALID) {
        return std::nullopt;
    }
    uint8_t* page = bp_.fetch(table_id_, root_id);
    const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
    uint16_t level = h->level;
    uint32_t current_id = root_id;
    bp_.unpin(table_id_, root_id);

    while (level != static_cast<uint16_t>(PageLevel::PAGE_LEAF)) {
        page = bp_.fetch(table_id_, current_id);
        const PageHeader* h_int = reinterpret_cast<const PageHeader*>(page);
        uint32_t child_id = get_internal_child_at_slot(page, 0);
        bp_.unpin(table_id_, current_id);
        current_id = child_id;
        page = bp_.fetch(table_id_, current_id);
        h = reinterpret_cast<const PageHeader*>(page);
        level = h->level;
        bp_.unpin(table_id_, current_id);
    }
    return std::make_pair(current_id, static_cast<uint16_t>(0));
}

bool BPlusTree::key_exists(const uint8_t* key, uint16_t key_len) {
    auto pos = search(key, key_len);
    if (!pos) return false;
    uint8_t* page = bp_.fetch(table_id_, pos->first);
    uint16_t rec_key_len;
    const uint8_t* rec_key = get_key_at_slot(page, pos->second, &rec_key_len);
    bool exists = (rec_key_len == key_len && std::memcmp(rec_key, key, key_len) == 0 &&
                   !is_record_deleted(page, pos->second));
    bp_.unpin(table_id_, pos->first);
    return exists;
}

std::optional<std::pair<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>, ScanPosition>>
BPlusTree::next_entry_from(uint32_t page_id, uint16_t slot_index) {
    uint8_t* page = bp_.fetch(table_id_,page_id);
    const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
    while (slot_index < h->cell_count && is_record_deleted(page, slot_index))
        slot_index++;
    if (slot_index >= h->cell_count) {
        uint32_t next_id = get_leaf_next_page_id(page);
        bp_.unpin(table_id_,page_id);
        if (next_id == 0xFFFFFFFFu) return std::nullopt;
        return next_entry_from(next_id, 0);
    }
    uint16_t key_len, row_len;
    const uint8_t* key_ptr = get_key_at_slot(page, slot_index, &key_len);
    const uint8_t* row_ptr = get_row_at_slot(page, slot_index, &row_len);
    std::vector<uint8_t> key(key_ptr, key_ptr + key_len);
    // Strip row_id (first 8 bytes) from row data before returning
    if (row_len < 8) {
        bp_.unpin(table_id_, page_id);
        throw std::runtime_error("Row data too small to contain row_id");
    }
    std::vector<uint8_t> row(row_ptr + 8, row_ptr + row_len);
    ScanPosition next_pos;
    next_pos.page_id = page_id;
    next_pos.slot_index = slot_index + 1;
    bp_.unpin(table_id_,page_id);
    return std::make_pair(std::make_pair(std::move(key), std::move(row)), next_pos);
}

int BPlusTree::remove(const uint8_t* key, uint16_t key_len) {
    auto pos = search(key, key_len);
    if (!pos) return 0;
    uint32_t leaf_id = pos->first;
    uint16_t slot_idx = pos->second;
    int deleted = 0;

    for (;;) {
        uint8_t* page = bp_.fetch(table_id_, leaf_id);
        PageHeader* h = reinterpret_cast<PageHeader*>(page);

        if (slot_idx >= h->cell_count) {
            uint32_t next_id = get_leaf_next_page_id(page);
            bp_.unpin(table_id_, leaf_id);
            if (next_id == 0xFFFFFFFFu) break;
            leaf_id = next_id;
            slot_idx = 0;
            continue;
        }

        uint16_t rec_key_len;
        const uint8_t* rec_key = get_key_at_slot(page, slot_idx, &rec_key_len);
        int cmp = (rec_key_len != key_len) ? (rec_key_len < key_len ? -1 : 1)
                 : std::memcmp(rec_key, key, key_len);
        if (cmp > 0) {
            bp_.unpin(table_id_, leaf_id);
            break;
        }
        if (cmp == 0 && !is_record_deleted(page, slot_idx)) {
            set_record_deleted(page, slot_idx);
            bp_.mark_dirty(table_id_, leaf_id);
            deleted++;

            uint32_t dead_bytes = compute_dead_bytes_leaf(page);
            if (dead_bytes >= static_cast<uint32_t>(PURGE_DEAD_RATIO * PAGE_SIZE)) {
                compact_leaf_page(page);
                bp_.mark_dirty(table_id_, leaf_id);
                h = reinterpret_cast<PageHeader*>(page);
                if (h->cell_count == 0) {
                    uint32_t prev_id = get_leaf_prev_page_id(page);
                    uint32_t next_id = get_leaf_next_page_id(page);
                    uint32_t parent_page_id = h->parent_page;
                    bp_.unpin(table_id_, leaf_id);
                    unlink_empty_leaf_and_add_to_freelist(leaf_id, prev_id, next_id, parent_page_id);
                    if (next_id == 0xFFFFFFFFu) break;
                    leaf_id = next_id;
                    slot_idx = 0;
                    continue;
                }
                // After compact, same slot_idx may hold next record; do not increment
            } else {
                slot_idx++;
            }
            bp_.unpin(table_id_, leaf_id);
            continue;
        }
        slot_idx++;
        bp_.unpin(table_id_, leaf_id);
    }
    return deleted;
}

void BPlusTree::unlink_empty_leaf_and_add_to_freelist(uint32_t leaf_page_id, uint32_t prev_id, uint32_t next_id, uint32_t parent_page_id) {
    if (prev_id != 0xFFFFFFFFu) {
        uint8_t* prev_page = bp_.fetch(table_id_, prev_id);
        set_leaf_next_page_id(prev_page, next_id);
        bp_.mark_dirty(table_id_, prev_id);
        bp_.unpin(table_id_, prev_id);
    }
    if (next_id != 0xFFFFFFFFu) {
        uint8_t* next_page = bp_.fetch(table_id_, next_id);
        set_leaf_prev_page_id(next_page, prev_id);
        bp_.mark_dirty(table_id_, next_id);
        bp_.unpin(table_id_, next_id);
    }
    if (parent_page_id != 0) {
        uint8_t* parent_page = bp_.fetch(table_id_, parent_page_id);
        remove_child_from_internal_page(parent_page, parent_page_id, leaf_page_id);
        bp_.mark_dirty(table_id_, parent_page_id);
        bp_.unpin(table_id_, parent_page_id);
    }
    uint8_t* page0 = get_page0();
    uint32_t root_id = get_root_page_id(page0);
    if (root_id == leaf_page_id) {
        set_root_page_id(page0, ROOT_PAGE_ID_INVALID);
        mark_page0_dirty();
    }
    push_free_page(page0, leaf_page_id);
    mark_page0_dirty();
}

void BPlusTree::insert(const uint8_t* key, uint16_t key_len, const uint8_t* row, uint16_t row_len, Transaction& txn) {
    (void)txn;
    uint8_t* page0 = get_page0();
    uint32_t root_id = get_root_page_id(page0);
    if (root_id == ROOT_PAGE_ID_INVALID) {
        uint32_t leaf_id = allocate_page();
        uint8_t* leaf = bp_.fetch(table_id_,leaf_id);
        init_index_leaf_page(leaf, leaf_id);
        bp_.mark_dirty(table_id_,leaf_id);
        bp_.unpin(table_id_,leaf_id);
        set_root_page_id(page0, leaf_id);
        mark_page0_dirty();
        page0 = get_page0();
        root_id = leaf_id;
    }

    std::vector<std::pair<uint32_t, int>> path;
    uint32_t current_id = root_id;
    uint8_t* page = bp_.fetch(table_id_,current_id);
    PageHeader* h = reinterpret_cast<PageHeader*>(page);
    path.push_back({current_id, -1});

    while (h->level != static_cast<uint16_t>(PageLevel::PAGE_LEAF)) {
        int slot_idx = binary_search_slots_internal(page, key, key_len);
        uint32_t child_id;
        // slot_idx = first slot where slot_key >= search_key
        // If slot_idx >= cell_count: key > all separators, go to rightmost
        // Else: key <= slot[slot_idx].key, go to slot[slot_idx].child
        if (slot_idx >= static_cast<int>(h->cell_count)) {
            child_id = get_internal_rightmost(page);
        } else {
            child_id = get_internal_child_at_slot(page, static_cast<uint16_t>(slot_idx));
        }
        bp_.unpin(table_id_,current_id);
        path.push_back({child_id, slot_idx});
        current_id = child_id;
        page = bp_.fetch(table_id_,current_id);
        h = reinterpret_cast<PageHeader*>(page);
    }

    int slot_idx = binary_search_slots(page, key, key_len);
    bool ok = insert_record_leaf(page, static_cast<uint16_t>(slot_idx), key, key_len, row, row_len);
    bp_.mark_dirty(table_id_,current_id);
    bp_.unpin(table_id_,current_id);

    if (!ok) {
        uint32_t parent_id = path.size() >= 2 ? path[path.size() - 2].first : 0;
        int slot_in_parent = path.back().second;
        split_leaf(current_id, parent_id, slot_in_parent);
        insert(key, key_len, row, row_len, txn);
    }
}

void BPlusTree::split_leaf(uint32_t leaf_page_id, uint32_t parent_page_id, int slot_in_parent) {
    uint8_t* leaf = bp_.fetch(table_id_,leaf_page_id);
    PageHeader* leaf_h = reinterpret_cast<PageHeader*>(leaf);
    uint16_t cell_count = leaf_h->cell_count;
    uint16_t mid = cell_count / 2;

    uint32_t new_leaf_id = allocate_page();
    uint8_t* new_leaf = bp_.fetch(table_id_,new_leaf_id);
    init_index_leaf_page(new_leaf, new_leaf_id);
    set_leaf_prev_page_id(new_leaf, leaf_page_id);
    uint32_t old_next = get_leaf_next_page_id(leaf);
    set_leaf_next_page_id(leaf, new_leaf_id);
    set_leaf_next_page_id(new_leaf, old_next);

    if (old_next != 0xFFFFFFFFu) {
        uint8_t* old_next_page = bp_.fetch(table_id_,old_next);
        set_leaf_prev_page_id(old_next_page, new_leaf_id);
        bp_.mark_dirty(table_id_,old_next);
        bp_.unpin(table_id_,old_next);
    }
    bp_.mark_dirty(table_id_,leaf_page_id);
    bp_.unpin(table_id_,leaf_page_id);

    leaf = bp_.fetch(table_id_,leaf_page_id);
    for (uint16_t i = mid; i < cell_count; ++i) {
        uint16_t key_len, row_len;
        const uint8_t* key_ptr = get_key_at_slot(leaf, i, &key_len);
        const uint8_t* row_ptr = get_row_at_slot(leaf, i, &row_len);
        insert_record_leaf(new_leaf, static_cast<uint16_t>(i - mid), key_ptr, key_len, row_ptr, row_len);
    }
    leaf_h = reinterpret_cast<PageHeader*>(leaf);
    leaf_h->cell_count = mid;
    // NOTE: Do NOT adjust free_end! The slot directory starts at free_end and grows upward.
    // Adjusting free_end would shift the slot directory base, causing wrong slots to be read.

    bp_.mark_dirty(table_id_,leaf_page_id);
    bp_.unpin(table_id_,leaf_page_id);
    bp_.mark_dirty(table_id_,new_leaf_id);
    bp_.unpin(table_id_,new_leaf_id);

    new_leaf = bp_.fetch(table_id_,new_leaf_id);
    uint16_t first_key_len;
    const uint8_t* first_key = get_key_at_slot(new_leaf, 0, &first_key_len);
    bp_.unpin(table_id_,new_leaf_id);
    uint8_t* page0 = get_page0();
    if (parent_page_id == 0) {
        uint32_t new_root_id = allocate_page();
        uint8_t* root = bp_.fetch(table_id_,new_root_id);
        init_index_internal_page(root, new_root_id);
        set_internal_rightmost(root, new_leaf_id);
        insert_record_internal(root, 0, first_key, first_key_len, leaf_page_id);
        bp_.mark_dirty(table_id_,new_root_id);
        bp_.unpin(table_id_,new_root_id);
        set_root_page_id(page0, new_root_id);
        mark_page0_dirty();
    } else {
        uint8_t* parent = bp_.fetch(table_id_,parent_page_id);
        int idx = binary_search_slots_internal(parent, first_key, first_key_len);
        uint32_t child_for_new_slot = (idx == 0) ? leaf_page_id : get_internal_rightmost(parent);
        bool ok = insert_record_internal(parent, static_cast<uint16_t>(idx), first_key, first_key_len, child_for_new_slot);
        if (!ok) {
            bp_.unpin(table_id_,parent_page_id);
            split_internal(parent_page_id, 0, 0);
            parent = bp_.fetch(table_id_,parent_page_id);
            idx = binary_search_slots_internal(parent, first_key, first_key_len);
            child_for_new_slot = (idx == 0) ? leaf_page_id : get_internal_rightmost(parent);
            insert_record_internal(parent, static_cast<uint16_t>(idx), first_key, first_key_len, child_for_new_slot);
        }
        PageHeader* parent_h = reinterpret_cast<PageHeader*>(parent);
        bool used_rightmost = (static_cast<uint16_t>(idx + 1) >= parent_h->cell_count);
        if (!used_rightmost) {
            set_internal_child_at_slot(parent, static_cast<uint16_t>(idx + 1), new_leaf_id);
        } else {
            set_internal_rightmost(parent, new_leaf_id);
        }
        bp_.mark_dirty(table_id_,parent_page_id);
        bp_.unpin(table_id_,parent_page_id);
    }
}

void BPlusTree::split_internal(uint32_t internal_page_id, uint32_t parent_page_id, int slot_in_parent) {
    (void)internal_page_id;
    (void)parent_page_id;
    (void)slot_in_parent;
}
