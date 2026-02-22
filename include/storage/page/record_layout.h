#ifndef RECORD_LAYOUT_H
#define RECORD_LAYOUT_H

#include "storage/page/page.h"
#include <cstdint>
#include <cstring>

// Record flags (per pageEntry spec)
enum RecordFlags : uint8_t {
    REC_FLAG_DELETED = 1 << 0,
    REC_FLAG_MOVED   = 1 << 1
};

// Common record header: flags (1B) + key_size (2B)
struct RecordHeader {
    uint8_t  flags;
    uint16_t key_size;
};

constexpr uint16_t RECORD_HEADER_SIZE = sizeof(RecordHeader);  // 3 bytes

// Leaf page: after PageHeader, next_page_id (4B), prev_page_id (4B)
constexpr uint16_t INDEX_LEAF_NEXT_OFFSET = sizeof(PageHeader);
constexpr uint16_t INDEX_LEAF_PREV_OFFSET = INDEX_LEAF_NEXT_OFFSET + sizeof(uint32_t);
constexpr uint16_t INDEX_LEAF_FIRST_RECORD_OFFSET = INDEX_LEAF_PREV_OFFSET + sizeof(uint32_t);

// Internal page: after PageHeader, rightmost child (4B)
constexpr uint16_t INDEX_INTERNAL_RIGHTMOST_OFFSET = sizeof(PageHeader);
constexpr uint16_t INDEX_INTERNAL_FIRST_RECORD_OFFSET = INDEX_INTERNAL_RIGHTMOST_OFFSET + sizeof(uint32_t);

// Slot entry size (per pageSlotDirectory: uint16_t)
constexpr uint16_t SLOT_ENTRY_SIZE = sizeof(uint16_t);

// Leaf record: RecordHeader (3B) + value_size (2B) + key + value
inline uint16_t get_leaf_value_size_offset(const uint8_t* rec) {
    return RECORD_HEADER_SIZE;  // value_size starts after RecordHeader
}

// Internal record: RecordHeader (3B) + child_page_id (4B) + key
inline uint16_t get_internal_child_offset(const uint8_t* rec) {
    return RECORD_HEADER_SIZE;
}

// Helpers: init pages
void init_index_leaf_page(uint8_t* page, uint32_t page_id);
void init_index_internal_page(uint8_t* page, uint32_t page_id);

// Slot directory: cell_count slots at free_end; each slot is uint16_t offset
uint16_t get_slot_offset(const uint8_t* page, uint16_t slot_index);
void set_slot_offset(uint8_t* page, uint16_t slot_index, uint16_t offset);

// Binary search slots by key; returns slot index where key >= search_key (first >=)
int binary_search_slots(const uint8_t* page, const uint8_t* key, uint16_t key_len);

// Insert record into leaf at slot position; shift slots; update free_start, free_end, cell_count
bool insert_record_leaf(uint8_t* page, uint16_t slot_index, const uint8_t* key, uint16_t key_len,
                        const uint8_t* value, uint16_t value_len);
bool insert_record_internal(uint8_t* page, uint16_t slot_index, const uint8_t* key, uint16_t key_len,
                            uint32_t child_page_id);

// Read key/row at slot (leaf)
const uint8_t* get_key_at_slot(const uint8_t* page, uint16_t slot_index, uint16_t* key_len_out);
const uint8_t* get_row_at_slot(const uint8_t* page, uint16_t slot_index, uint16_t* row_len_out);

// Soft delete: check/set REC_FLAG_DELETED at record for slot (leaf only)
bool is_record_deleted(const uint8_t* page, uint16_t slot_index);
void set_record_deleted(uint8_t* page, uint16_t slot_index);

// Dead bytes and purge (leaf only). No PageHeader change; computed on the fly.
uint32_t compute_dead_bytes_leaf(const uint8_t* page);
void compact_leaf_page(uint8_t* page);

constexpr double PURGE_DEAD_RATIO = 0.15;

// Internal page: get/set child_page_id and key at slot
uint32_t get_internal_child_at_slot(const uint8_t* page, uint16_t slot_index);
void set_internal_child_at_slot(uint8_t* page, uint16_t slot_index, uint32_t child_page_id);
const uint8_t* get_internal_key_at_slot(const uint8_t* page, uint16_t slot_index, uint16_t* key_len_out);
int binary_search_slots_internal(const uint8_t* page, const uint8_t* key, uint16_t key_len);

// Get next_page_id / prev_page_id from leaf
uint32_t get_leaf_next_page_id(const uint8_t* page);
uint32_t get_leaf_prev_page_id(const uint8_t* page);
void set_leaf_next_page_id(uint8_t* page, uint32_t next_id);
void set_leaf_prev_page_id(uint8_t* page, uint32_t prev_id);

// Get/set rightmost child on internal page
uint32_t get_internal_rightmost(const uint8_t* page);
void set_internal_rightmost(uint8_t* page, uint32_t page_id);

// Remove one child (by page_id) from internal page; compact. Preserves page_id. Used when unlinking empty leaf.
void remove_child_from_internal_page(uint8_t* page, uint32_t page_id, uint32_t child_page_id);

#endif // RECORD_LAYOUT_H
