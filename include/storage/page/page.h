#ifndef PAGE_H
#define PAGE_H

#include <cstdint>
#include <cstring>

// Page size constant (8KB)
constexpr uint16_t PAGE_SIZE = 8192;

// PageKind enum
enum class PageKind : uint16_t {
    PAGE_FREE = 0,
    PAGE_HEADER = 1,
    PAGE_META = 2,
    PAGE_DATA = 3,
    PAGE_INDEX = 4
};

// PageLevel enum
enum class PageLevel : uint16_t {
    PAGE_LEAF = 0,
    PAGE_INTERNAL = 1
};

// PageFlags enum
enum PageFlags : uint16_t {
    PAGE_FLAG_DIRTY = 1 << 0,
    PAGE_FLAG_ROOT = 1 << 1,
    PAGE_FLAG_UNIQUE = 1 << 2,
    PAGE_FLAG_DELETED = 1 << 3
};

// Standard PageHeader (32 bytes) - matches spec
struct PageHeader {
    uint32_t page_id;        // page number
    uint16_t kind;           // PageKind
    uint16_t level;          // PageLevel
    uint16_t flags;          // PageFlags
    uint16_t cell_count;     // number of active cells
    uint16_t free_start;     // offset to start of free space
    uint16_t free_end;       // offset to start of slot directory
    uint32_t parent_page;    // parent page id (INDEX only)
    uint32_t lsn;            // last WAL LSN applied
};

// Page 0 Table Header Layout
// Layout: [PageHeader (32B)] [root_page_id (4B)] [table_name_len (2B)] [table_name (max 256B)] [db_name_len (2B)] [db_name (max 256B)] [free_page_count (4B)] [free_pages[] (4B each)]
constexpr uint16_t PAGE0_ROOT_PAGE_ID_OFFSET = sizeof(PageHeader);
constexpr uint32_t ROOT_PAGE_ID_INVALID = 0xFFFFFFFFU;

constexpr uint16_t PAGE0_TABLE_NAME_LEN_OFFSET = PAGE0_ROOT_PAGE_ID_OFFSET + sizeof(uint32_t);  // 36
constexpr uint16_t PAGE0_TABLE_NAME_OFFSET = PAGE0_TABLE_NAME_LEN_OFFSET + sizeof(uint16_t);   // 38
constexpr uint16_t PAGE0_TABLE_NAME_MAX_LEN = 256;
constexpr uint16_t PAGE0_DB_NAME_LEN_OFFSET = PAGE0_TABLE_NAME_OFFSET + PAGE0_TABLE_NAME_MAX_LEN;  // 294
constexpr uint16_t PAGE0_DB_NAME_OFFSET = PAGE0_DB_NAME_LEN_OFFSET + sizeof(uint16_t);           // 296
constexpr uint16_t PAGE0_DB_NAME_MAX_LEN = 256;
constexpr uint16_t PAGE0_FREE_COUNT_OFFSET = PAGE0_DB_NAME_OFFSET + PAGE0_DB_NAME_MAX_LEN;  // 552
// Row ID counter (8 bytes) - auto-incrementing per table
constexpr uint16_t PAGE0_NEXT_ROW_ID_OFFSET = PAGE0_FREE_COUNT_OFFSET + sizeof(uint32_t);  // 556
// AUTO_INCREMENT counters: 8 slots x 8 bytes (max 8 AUTO_INCREMENT columns per table)
constexpr uint16_t PAGE0_AI_COUNTERS_OFFSET = PAGE0_NEXT_ROW_ID_OFFSET + sizeof(uint64_t);   // 564
constexpr uint16_t PAGE0_AI_COUNTER_COUNT = 8;
constexpr uint16_t PAGE0_FREE_LIST_OFFSET = PAGE0_AI_COUNTERS_OFFSET + PAGE0_AI_COUNTER_COUNT * sizeof(uint64_t);  // 628
constexpr uint16_t PAGE0_MAX_FREE_PAGES = (PAGE_SIZE - PAGE0_FREE_LIST_OFFSET) / sizeof(uint32_t);

// Inline helpers for root_page_id on page 0
inline uint32_t get_root_page_id(const uint8_t* page0) {
    uint32_t v;
    std::memcpy(&v, page0 + PAGE0_ROOT_PAGE_ID_OFFSET, sizeof(uint32_t));
    return v;
}
inline void set_root_page_id(uint8_t* page0, uint32_t root_page_id) {
    std::memcpy(page0 + PAGE0_ROOT_PAGE_ID_OFFSET, &root_page_id, sizeof(uint32_t));
}

// Inline helpers for row_id on page 0
inline uint64_t get_next_row_id(const uint8_t* page0) {
    uint64_t v;
    std::memcpy(&v, page0 + PAGE0_NEXT_ROW_ID_OFFSET, sizeof(uint64_t));
    return v;
}
inline void set_next_row_id(uint8_t* page0, uint64_t row_id) {
    std::memcpy(page0 + PAGE0_NEXT_ROW_ID_OFFSET, &row_id, sizeof(uint64_t));
}

// AUTO_INCREMENT counters on page 0 (column_index 0..PAGE0_AI_COUNTER_COUNT-1)
inline uint64_t get_auto_increment_counter(const uint8_t* page0, uint16_t column_index) {
    uint64_t v;
    std::memcpy(&v, page0 + PAGE0_AI_COUNTERS_OFFSET + column_index * sizeof(uint64_t), sizeof(uint64_t));
    return v;
}
inline void set_auto_increment_counter(uint8_t* page0, uint16_t column_index, uint64_t value) {
    std::memcpy(page0 + PAGE0_AI_COUNTERS_OFFSET + column_index * sizeof(uint64_t), &value, sizeof(uint64_t));
}

// Free list on page 0 (for page allocation)
inline uint32_t get_free_page_count(const uint8_t* page0) {
    uint32_t v;
    std::memcpy(&v, page0 + PAGE0_FREE_COUNT_OFFSET, sizeof(uint32_t));
    return v;
}
inline void set_free_page_count(uint8_t* page0, uint32_t count) {
    std::memcpy(page0 + PAGE0_FREE_COUNT_OFFSET, &count, sizeof(uint32_t));
}
inline uint32_t get_free_page_at(const uint8_t* page0, uint32_t index) {
    uint32_t v;
    std::memcpy(&v, page0 + PAGE0_FREE_LIST_OFFSET + index * sizeof(uint32_t), sizeof(uint32_t));
    return v;
}
inline void set_free_page_at(uint8_t* page0, uint32_t index, uint32_t page_id) {
    std::memcpy(page0 + PAGE0_FREE_LIST_OFFSET + index * sizeof(uint32_t), &page_id, sizeof(uint32_t));
}
inline bool pop_free_page(uint8_t* page0, uint32_t* page_id_out) {
    uint32_t count = get_free_page_count(page0);
    if (count == 0) return false;
    *page_id_out = get_free_page_at(page0, 0);
    for (uint32_t i = 1; i < count; ++i) {
        uint32_t p = get_free_page_at(page0, i);
        set_free_page_at(page0, i - 1, p);
    }
    set_free_page_count(page0, count - 1);
    return true;
}
inline bool push_free_page(uint8_t* page0, uint32_t page_id) {
    uint32_t count = get_free_page_count(page0);
    if (count >= PAGE0_MAX_FREE_PAGES) return false;
    set_free_page_at(page0, count, page_id);
    set_free_page_count(page0, count + 1);
    return true;
}

// Page 1 Meta/Catalog Layout (schema storage)
// Layout: [PageHeader (32B)] [schema_size (2B)] [schema_data (variable)]
constexpr uint16_t PAGE1_SCHEMA_SIZE_OFFSET = sizeof(PageHeader);
constexpr uint16_t PAGE1_SCHEMA_DATA_OFFSET = PAGE1_SCHEMA_SIZE_OFFSET + sizeof(uint16_t);
constexpr uint16_t PAGE1_MAX_SCHEMA_SIZE = PAGE_SIZE - PAGE1_SCHEMA_DATA_OFFSET;

// Legacy constants for backward compatibility (deprecated - use PAGE1_* instead)
constexpr uint16_t PAGE0_SCHEMA_SIZE_OFFSET = PAGE1_SCHEMA_SIZE_OFFSET;
constexpr uint16_t PAGE0_SCHEMA_DATA_OFFSET = PAGE1_SCHEMA_DATA_OFFSET;
constexpr uint16_t PAGE0_MAX_SCHEMA_SIZE = PAGE1_MAX_SCHEMA_SIZE;

#endif // PAGE_H
