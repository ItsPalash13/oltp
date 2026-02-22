#include "storage/page/record_layout.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <vector>

static constexpr uint32_t INVALID_PAGE_ID = 0xFFFFFFFFu;

// Leaf record: RecordHeader (3B) + value_size (2B) + key + value
constexpr uint16_t LEAF_HEADER_SIZE = RECORD_HEADER_SIZE + sizeof(uint16_t);  // 5 bytes

// Internal record: RecordHeader (3B) + child_page_id (4B) + key
constexpr uint16_t INTERNAL_HEADER_SIZE = RECORD_HEADER_SIZE + sizeof(uint32_t);  // 7 bytes

void init_index_leaf_page(uint8_t* page, uint32_t page_id) {
    std::memset(page, 0, PAGE_SIZE);
    PageHeader* h = reinterpret_cast<PageHeader*>(page);
    h->page_id = page_id;
    h->kind = static_cast<uint16_t>(PageKind::PAGE_INDEX);
    h->level = static_cast<uint16_t>(PageLevel::PAGE_LEAF);
    h->flags = 0;
    h->cell_count = 0;
    h->free_start = INDEX_LEAF_FIRST_RECORD_OFFSET;
    h->free_end = PAGE_SIZE;
    h->parent_page = 0;
    h->lsn = 0;
    // next/prev = INVALID
    uint32_t inv = INVALID_PAGE_ID;
    std::memcpy(page + INDEX_LEAF_NEXT_OFFSET, &inv, sizeof(uint32_t));
    std::memcpy(page + INDEX_LEAF_PREV_OFFSET, &inv, sizeof(uint32_t));
}

void init_index_internal_page(uint8_t* page, uint32_t page_id) {
    std::memset(page, 0, PAGE_SIZE);
    PageHeader* h = reinterpret_cast<PageHeader*>(page);
    h->page_id = page_id;
    h->kind = static_cast<uint16_t>(PageKind::PAGE_INDEX);
    h->level = static_cast<uint16_t>(PageLevel::PAGE_INTERNAL);
    h->flags = 0;
    h->cell_count = 0;
    h->free_start = INDEX_INTERNAL_FIRST_RECORD_OFFSET;
    h->free_end = PAGE_SIZE;
    h->parent_page = 0;
    h->lsn = 0;
    uint32_t inv = INVALID_PAGE_ID;
    std::memcpy(page + INDEX_INTERNAL_RIGHTMOST_OFFSET, &inv, sizeof(uint32_t));
}

uint16_t get_slot_offset(const uint8_t* page, uint16_t slot_index) {
    const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
    if (slot_index >= h->cell_count) {
        throw std::runtime_error("get_slot_offset: slot_index out of range");
    }
    uint16_t slot_dir = h->free_end + slot_index * SLOT_ENTRY_SIZE;
    uint16_t off;
    std::memcpy(&off, page + slot_dir, sizeof(uint16_t));
    return off;
}

void set_slot_offset(uint8_t* page, uint16_t slot_index, uint16_t offset) {
    PageHeader* h = reinterpret_cast<PageHeader*>(page);
    if (slot_index >= h->cell_count) {
        throw std::runtime_error("set_slot_offset: slot_index out of range");
    }
    uint16_t slot_dir = h->free_end + slot_index * SLOT_ENTRY_SIZE;
    std::memcpy(page + slot_dir, &offset, sizeof(uint16_t));
}

static int compare_key_at_slot(const uint8_t* page, uint16_t slot_index, const uint8_t* key, uint16_t key_len) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    const uint8_t* rec = page + rec_off;
    uint16_t rec_key_len;
    std::memcpy(&rec_key_len, rec + 1, sizeof(uint16_t));
    const uint8_t* rec_key = rec + LEAF_HEADER_SIZE;  // same for internal: key follows header (value_size for leaf, child_id for internal)
    // For internal record key starts after RecordHeader + child_page_id
    // We need to know page type - for binary_search we're comparing keys. Leaf has RecordHeader+value_size+key; internal has RecordHeader+child_page_id+key.
    // So for leaf rec_key = rec + 5, for internal rec_key = rec + 7. We use a common layout: both have key at rec + (header_size). So we need to pass or detect. For simplicity, assume leaf in binary_search (used in leaf pages). So rec_key = rec + LEAF_HEADER_SIZE, rec_key_len from RecordHeader.
    size_t cmp_len = std::min(static_cast<size_t>(rec_key_len), static_cast<size_t>(key_len));
    int c = std::memcmp(rec_key, key, cmp_len);
    if (c != 0) return c;
    if (rec_key_len < key_len) return -1;
    if (rec_key_len > key_len) return 1;
    return 0;
}

// For leaf pages: key is at rec + LEAF_HEADER_SIZE
static int compare_key_leaf(const uint8_t* page, uint16_t slot_index, const uint8_t* key, uint16_t key_len) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    const uint8_t* rec = page + rec_off;
    uint16_t rec_key_len;
    std::memcpy(&rec_key_len, rec + 1, sizeof(uint16_t));
    const uint8_t* rec_key = rec + LEAF_HEADER_SIZE;
    size_t cmp_len = std::min(static_cast<size_t>(rec_key_len), static_cast<size_t>(key_len));
    int c = std::memcmp(rec_key, key, cmp_len);
    if (c != 0) return c;
    if (rec_key_len < key_len) return -1;
    if (rec_key_len > key_len) return 1;
    return 0;
}

int binary_search_slots(const uint8_t* page, const uint8_t* key, uint16_t key_len) {
    const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
    int lo = 0, hi = static_cast<int>(h->cell_count);
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        int c = compare_key_leaf(page, static_cast<uint16_t>(mid), key, key_len);
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

bool insert_record_leaf(uint8_t* page, uint16_t slot_index, const uint8_t* key, uint16_t key_len,
                        const uint8_t* value, uint16_t value_len) {
    PageHeader* h = reinterpret_cast<PageHeader*>(page);
    uint16_t record_size = LEAF_HEADER_SIZE + key_len + value_len;
    if (h->free_end - h->free_start < record_size + SLOT_ENTRY_SIZE) {
        return false;
    }
    uint8_t* rec = page + h->free_start;
    rec[0] = 0;
    std::memcpy(rec + 1, &key_len, sizeof(uint16_t));
    std::memcpy(rec + 3, &value_len, sizeof(uint16_t));
    std::memcpy(rec + LEAF_HEADER_SIZE, key, key_len);
    std::memcpy(rec + LEAF_HEADER_SIZE + key_len, value, value_len);
    uint16_t rec_offset = h->free_start;
    h->free_start += record_size;
    h->free_end -= SLOT_ENTRY_SIZE;
    uint16_t slot_start = h->free_end;
    std::memmove(page + slot_start, page + slot_start + SLOT_ENTRY_SIZE, h->cell_count * SLOT_ENTRY_SIZE);
    if (slot_index < h->cell_count) {
        std::memmove(page + slot_start + (slot_index + 1) * SLOT_ENTRY_SIZE,
                     page + slot_start + slot_index * SLOT_ENTRY_SIZE,
                     (h->cell_count - slot_index) * SLOT_ENTRY_SIZE);
    }
    std::memcpy(page + slot_start + slot_index * SLOT_ENTRY_SIZE, &rec_offset, sizeof(uint16_t));
    h->cell_count++;
    return true;
}

bool insert_record_internal(uint8_t* page, uint16_t slot_index, const uint8_t* key, uint16_t key_len,
                            uint32_t child_page_id) {
    PageHeader* h = reinterpret_cast<PageHeader*>(page);
    uint16_t record_size = INTERNAL_HEADER_SIZE + key_len;
    if (h->free_end - h->free_start < record_size + SLOT_ENTRY_SIZE) {
        return false;
    }
    uint8_t* rec = page + h->free_start;
    rec[0] = 0;
    std::memcpy(rec + 1, &key_len, sizeof(uint16_t));
    std::memcpy(rec + RECORD_HEADER_SIZE, &child_page_id, sizeof(uint32_t));
    std::memcpy(rec + INTERNAL_HEADER_SIZE, key, key_len);
    uint16_t rec_offset = h->free_start;
    h->free_start += record_size;
    h->free_end -= SLOT_ENTRY_SIZE;
    uint16_t slot_start = h->free_end;
    std::memmove(page + slot_start, page + slot_start + SLOT_ENTRY_SIZE, h->cell_count * SLOT_ENTRY_SIZE);
    if (slot_index < h->cell_count) {
        std::memmove(page + slot_start + (slot_index + 1) * SLOT_ENTRY_SIZE,
                     page + slot_start + slot_index * SLOT_ENTRY_SIZE,
                     (h->cell_count - slot_index) * SLOT_ENTRY_SIZE);
    }
    std::memcpy(page + slot_start + slot_index * SLOT_ENTRY_SIZE, &rec_offset, sizeof(uint16_t));
    h->cell_count++;
    return true;
}

bool is_record_deleted(const uint8_t* page, uint16_t slot_index) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    const uint8_t* rec = page + rec_off;
    return (rec[0] & static_cast<uint8_t>(REC_FLAG_DELETED)) != 0;
}

void set_record_deleted(uint8_t* page, uint16_t slot_index) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    uint8_t* rec = page + rec_off;
    rec[0] |= static_cast<uint8_t>(REC_FLAG_DELETED);
}

uint32_t compute_dead_bytes_leaf(const uint8_t* page) {
    const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
    uint32_t dead = 0;
    for (uint16_t i = 0; i < h->cell_count; ++i) {
        if (!is_record_deleted(page, i)) continue;
        uint16_t rec_off = get_slot_offset(page, i);
        const uint8_t* rec = page + rec_off;
        uint16_t key_len, value_len;
        std::memcpy(&key_len, rec + 1, sizeof(uint16_t));
        std::memcpy(&value_len, rec + 3, sizeof(uint16_t));
        dead += LEAF_HEADER_SIZE + key_len + value_len;
    }
    return dead;
}

struct LiveRecord {
    uint16_t key_len;
    uint16_t value_len;
    std::vector<uint8_t> key;
    std::vector<uint8_t> value;
};

void compact_leaf_page(uint8_t* page) {
    PageHeader* h = reinterpret_cast<PageHeader*>(page);
    uint32_t next_id = get_leaf_next_page_id(page);
    uint32_t prev_id = get_leaf_prev_page_id(page);

    std::vector<LiveRecord> live;
    for (uint16_t i = 0; i < h->cell_count; ++i) {
        if (is_record_deleted(page, i)) continue;
        uint16_t key_len, value_len;
        const uint8_t* key_ptr = get_key_at_slot(page, i, &key_len);
        const uint8_t* value_ptr = get_row_at_slot(page, i, &value_len);
        LiveRecord rec;
        rec.key_len = key_len;
        rec.value_len = value_len;
        rec.key.assign(key_ptr, key_ptr + key_len);
        rec.value.assign(value_ptr, value_ptr + value_len);
        live.push_back(std::move(rec));
    }

    uint16_t num_live = static_cast<uint16_t>(live.size());
    h->cell_count = num_live;
    h->free_start = INDEX_LEAF_FIRST_RECORD_OFFSET;
    h->free_end = PAGE_SIZE - num_live * SLOT_ENTRY_SIZE;

    uint16_t slot_dir = h->free_end;
    for (uint16_t i = 0; i < num_live; ++i) {
        const LiveRecord& rec = live[i];
        uint16_t record_size = LEAF_HEADER_SIZE + rec.key_len + rec.value_len;
        uint8_t* dest = page + h->free_start;
        dest[0] = 0;
        std::memcpy(dest + 1, &rec.key_len, sizeof(uint16_t));
        std::memcpy(dest + 3, &rec.value_len, sizeof(uint16_t));
        std::memcpy(dest + LEAF_HEADER_SIZE, rec.key.data(), rec.key_len);
        std::memcpy(dest + LEAF_HEADER_SIZE + rec.key_len, rec.value.data(), rec.value_len);
        uint16_t off = h->free_start;
        std::memcpy(page + slot_dir + i * SLOT_ENTRY_SIZE, &off, sizeof(uint16_t));
        h->free_start += record_size;
    }

    set_leaf_next_page_id(page, next_id);
    set_leaf_prev_page_id(page, prev_id);
}

const uint8_t* get_key_at_slot(const uint8_t* page, uint16_t slot_index, uint16_t* key_len_out) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    const uint8_t* rec = page + rec_off;
    std::memcpy(key_len_out, rec + 1, sizeof(uint16_t));
    return rec + LEAF_HEADER_SIZE;
}

const uint8_t* get_row_at_slot(const uint8_t* page, uint16_t slot_index, uint16_t* row_len_out) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    const uint8_t* rec = page + rec_off;
    uint16_t key_len;
    std::memcpy(&key_len, rec + 1, sizeof(uint16_t));
    std::memcpy(row_len_out, rec + 3, sizeof(uint16_t));
    return rec + LEAF_HEADER_SIZE + key_len;
}

uint32_t get_leaf_next_page_id(const uint8_t* page) {
    uint32_t v;
    std::memcpy(&v, page + INDEX_LEAF_NEXT_OFFSET, sizeof(uint32_t));
    return v;
}

uint32_t get_leaf_prev_page_id(const uint8_t* page) {
    uint32_t v;
    std::memcpy(&v, page + INDEX_LEAF_PREV_OFFSET, sizeof(uint32_t));
    return v;
}

void set_leaf_next_page_id(uint8_t* page, uint32_t next_id) {
    std::memcpy(page + INDEX_LEAF_NEXT_OFFSET, &next_id, sizeof(uint32_t));
}

void set_leaf_prev_page_id(uint8_t* page, uint32_t prev_id) {
    std::memcpy(page + INDEX_LEAF_PREV_OFFSET, &prev_id, sizeof(uint32_t));
}

uint32_t get_internal_rightmost(const uint8_t* page) {
    uint32_t v;
    std::memcpy(&v, page + INDEX_INTERNAL_RIGHTMOST_OFFSET, sizeof(uint32_t));
    return v;
}

void set_internal_rightmost(uint8_t* page, uint32_t page_id) {
    std::memcpy(page + INDEX_INTERNAL_RIGHTMOST_OFFSET, &page_id, sizeof(uint32_t));
}

uint32_t get_internal_child_at_slot(const uint8_t* page, uint16_t slot_index) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    const uint8_t* rec = page + rec_off;
    uint32_t child;
    std::memcpy(&child, rec + RECORD_HEADER_SIZE, sizeof(uint32_t));
    return child;
}

void set_internal_child_at_slot(uint8_t* page, uint16_t slot_index, uint32_t child_page_id) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    uint8_t* rec = page + rec_off;
    std::memcpy(rec + RECORD_HEADER_SIZE, &child_page_id, sizeof(uint32_t));
}

const uint8_t* get_internal_key_at_slot(const uint8_t* page, uint16_t slot_index, uint16_t* key_len_out) {
    uint16_t rec_off = get_slot_offset(page, slot_index);
    const uint8_t* rec = page + rec_off;
    std::memcpy(key_len_out, rec + 1, sizeof(uint16_t));
    return rec + INTERNAL_HEADER_SIZE;
}

static int compare_key_internal(const uint8_t* page, uint16_t slot_index, const uint8_t* key, uint16_t key_len) {
    uint16_t rec_key_len;
    const uint8_t* rec_key = get_internal_key_at_slot(page, slot_index, &rec_key_len);
    size_t cmp_len = std::min(static_cast<size_t>(rec_key_len), static_cast<size_t>(key_len));
    int c = std::memcmp(rec_key, key, cmp_len);
    if (c != 0) return c;
    if (rec_key_len < key_len) return -1;
    if (rec_key_len > key_len) return 1;
    return 0;
}

int binary_search_slots_internal(const uint8_t* page, const uint8_t* key, uint16_t key_len) {
    const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
    int lo = 0, hi = static_cast<int>(h->cell_count);
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        int c = compare_key_internal(page, static_cast<uint16_t>(mid), key, key_len);
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

struct InternalChild {
    uint16_t key_len;
    std::vector<uint8_t> key;
    uint32_t child_id;
};

void remove_child_from_internal_page(uint8_t* page, uint32_t page_id, uint32_t child_page_id) {
    const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
    std::vector<InternalChild> keep;
    for (uint16_t i = 0; i < h->cell_count; ++i) {
        uint32_t child = get_internal_child_at_slot(page, i);
        if (child == child_page_id) continue;
        uint16_t key_len;
        const uint8_t* key_ptr = get_internal_key_at_slot(page, i, &key_len);
        InternalChild c;
        c.key_len = key_len;
        c.key.assign(key_ptr, key_ptr + key_len);
        c.child_id = child;
        keep.push_back(std::move(c));
    }
    uint32_t rightmost = get_internal_rightmost(page);
    if (rightmost != child_page_id) {
        InternalChild c;
        c.key_len = 0;
        c.child_id = rightmost;
        keep.push_back(std::move(c));
    }
    if (keep.empty()) {
        init_index_internal_page(page, page_id);
        return;
    }
    init_index_internal_page(page, page_id);
    PageHeader* hw = reinterpret_cast<PageHeader*>(page);
    for (size_t i = 0; i + 1 < keep.size(); ++i) {
        const InternalChild& c = keep[i];
        insert_record_internal(page, static_cast<uint16_t>(i), c.key.data(), c.key_len, c.child_id);
    }
    set_internal_rightmost(page, keep.back().child_id);
}
