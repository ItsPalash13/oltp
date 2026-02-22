#ifndef CATALOG_MANAGER_H
#define CATALOG_MANAGER_H

#include "storage/page/page.h"
#include "parser/statements/create.h"
#include <array>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

/** Entry for CATALOG VIEW: one cached table's slot state. */
struct CatalogCacheEntry {
    int slot;
    std::string table_name;
    std::string db_path;
    uint64_t last_access_time;
    bool dirty;
};

/**
 * CatalogSlot - represents one entry in the hash index.
 * Maps a table name to a pool page slot.
 */
struct CatalogSlot {
    std::string table_name;      // Table name (empty if slot unused)
    std::string db_path;         // Database path (e.g., "@data/mydb/")
    uint8_t pool_slot_index;     // Which pool page (0-2)
    bool is_valid;               // Slot in use
    uint64_t last_access_time;    // For LRU eviction

    CatalogSlot() : pool_slot_index(0), is_valid(false), last_access_time(0) {}
};

/**
 * CatalogManager - caches table header (page 0) and meta (page 1) in memory.
 *
 * Design:
 * - Preallocated pool: 3 slots × 2 pages (0 & 1) × 8KB = 48KB total
 * - Hash index: 3 slots mapping table names to pool slots
 * - LRU eviction: When pool is full, evict least recently used slot (both pages)
 * - Dirty tracking: Per-page dirty; flush writes both pages when either is dirty
 *
 * Contract: Buffer pool (future) must NEVER cache pages 0 or 1; only page_id >= 2.
 * B+ tree reads root_page_id from catalog pool (get_page0), not from buffer pool.
 */
class CatalogManager {
private:
    // Preallocated catalog pool: 3 slots, each holds page 0 and page 1
    std::array<std::array<std::array<uint8_t, PAGE_SIZE>, 2>, 3> catalog_pool;

    // Hash index: 3 slots mapping table names to pool slots
    std::array<CatalogSlot, 3> hash_index;

    // Dirty flags per slot: [pool_slot][0]=page0, [pool_slot][1]=page1
    std::array<std::array<bool, 2>, 3> dirty_flags;

    // Access counter for LRU timestamps
    uint64_t access_counter;

    /**
     * Hash function: hash(table_name) % 3
     * @param table_name Table name to hash
     * @return Hash slot index (0-2)
     */
    uint32_t hash_table_name(const std::string& table_name) const;

    /**
     * Find a slot for a table in the hash index.
     * Returns slot index if found, or -1 if not found.
     * @param table_name Table name to find
     * @return Slot index (0-2) if found, -1 if not found
     */
    int find_slot(const std::string& table_name) const;

    /**
     * Find or allocate a slot for a table.
     * If table exists, returns its slot. Otherwise, finds empty slot or evicts LRU.
     * @param table_name Table name
     * @return Slot index (0-2) to use
     */
    uint8_t find_or_allocate_slot(const std::string& table_name);

    /**
     * Evict LRU slot and return its pool index.
     * Finds slot with oldest last_access_time.
     * @return Pool slot index (0-2) that was evicted
     */
    uint8_t evict_lru_slot();

    /**
     * Load pages 0 and 1 from disk into a pool slot.
     * @param db_path Database path (e.g., "@data/mydb/")
     * @param table_name Table name
     * @param pool_slot_index Which pool slot to load into (0-2)
     */
    void load_pages_to_slot(const std::string& db_path, const std::string& table_name, uint8_t pool_slot_index);

    /**
     * Write a pool slot's page 0 and/or page 1 to disk when dirty.
     * @param db_path Database path
     * @param table_name Table name
     * @param pool_slot_index Which pool slot to write (0-2)
     */
    void write_slot_to_disk(const std::string& db_path, const std::string& table_name, uint8_t pool_slot_index);

public:
    /**
     * Constructor - initialize catalog manager.
     * Preallocates 3 × 8KB = 24KB for catalog pool.
     */
    CatalogManager();

    /**
     * Load a table's meta page (page 1) from disk into catalog pool.
     * On-demand loading: if pool is full, evicts LRU slot.
     * @param db_path Database path (e.g., "@data/mydb/")
     * @param table_name Table name
     */
    void load_table_meta(const std::string& db_path, const std::string& table_name);

    /**
     * Get a table's header page (page 0) from catalog pool.
     * If not in pool, loads both pages from disk first.
     * Updates LRU timestamp.
     * @param db_path Database path
     * @param table_name Table name
     * @return Pointer to page 0 in pool (for root_page_id, free list)
     */
    uint8_t* get_page0(const std::string& db_path, const std::string& table_name);

    /**
     * Get a table's meta page (page 1) from catalog pool.
     * If not in pool, loads both pages from disk first.
     * Updates LRU timestamp.
     * @param db_path Database path
     * @param table_name Table name
     * @return Pointer to meta page in pool (8KB buffer)
     */
    uint8_t* get_table_meta(const std::string& db_path, const std::string& table_name);

    /**
     * Mark page 0 dirty for a table (e.g. after updating root_page_id or free list).
     * @param db_path Database path
     * @param table_name Table name
     */
    void mark_page0_dirty(const std::string& db_path, const std::string& table_name);

    /**
     * Create a new table and load its meta page into catalog pool.
     * Creates .ibd file with page 0 (table header) and page 1 (schema), then loads page 1 into pool.
     * @param db_path Database path
     * @param table_name Table name
     * @param schema Table schema
     */
    void create_table_meta(const std::string& db_path, const std::string& table_name, const CreateTableStmt& schema);

    /**
     * Read schema from cached meta page.
     * @param db_path Database path
     * @param table_name Table name
     * @return Deserialized schema
     */
    CreateTableStmt read_schema(const std::string& db_path, const std::string& table_name);

    /**
     * Update schema in catalog pool (mark dirty).
     * Modifies the cached meta page and marks it dirty for later flush.
     * @param db_path Database path
     * @param table_name Table name
     * @param schema New schema
     */
    void write_schema(const std::string& db_path, const std::string& table_name, const CreateTableStmt& schema);

    /**
     * Flush all dirty pages from catalog pool to disk.
     * Writes all modified meta pages to their .ibd files, then clears dirty flags.
     */
    void flush();

    /**
     * Clear catalog pool (evict all cached pages).
     * Flushes dirty pages first, then clears all slots.
     * Called when switching databases or dropping database.
     */
    void clear();

    /**
     * List tables currently in the catalog cache.
     * @return Pairs of (table_name, db_path) for each valid slot
     */
    std::vector<std::pair<std::string, std::string>> list_cached_tables() const;

    /**
     * View cache state: slot index, table name, db path, last_access_time, dirty.
     * @return One entry per valid slot
     */
    std::vector<CatalogCacheEntry> view_cache() const;

    /**
     * Evict a specific table from the cache by name.
     * If dirty, flushes to disk first. No-op if table not in cache.
     * @param table_name Table to evict
     */
    void evict_table(const std::string& table_name);

    /**
     * Get and increment row_id counter for a table.
     * @param db_path Database path
     * @param table_name Table name
     * @return Current row_id (before increment)
     */
    uint64_t get_and_increment_row_id(const std::string& db_path, const std::string& table_name);

    /**
     * Get and increment AUTO_INCREMENT counter for a table column.
     * @param db_path Database path
     * @param table_name Table name
     * @param column_index Index among AUTO_INCREMENT columns (0 = first AUTO_INCREMENT column in schema order)
     * @return Current counter value (before increment)
     */
    uint64_t get_and_increment_auto_increment(const std::string& db_path, const std::string& table_name, uint16_t column_index);
};

#endif // CATALOG_MANAGER_H
