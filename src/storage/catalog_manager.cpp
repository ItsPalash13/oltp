#include "storage/catalog_manager.h"
#include "storage/storage_manager.h"
#include "storage/schema_serializer.h"
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <iostream>

CatalogManager::CatalogManager() : access_counter(0) {
    // Initialize all slots as invalid
    for (auto& slot : hash_index) {
        slot.is_valid = false;
        slot.table_name = "";
        slot.db_path = "";
        slot.pool_slot_index = 0;
        slot.last_access_time = 0;
    }

    // Initialize all dirty flags to false
    for (auto& slot_flags : dirty_flags) {
        slot_flags[0] = false;
        slot_flags[1] = false;
    }

    // Zero out catalog pool (3 slots × 2 pages)
    for (auto& slot_pages : catalog_pool) {
        std::memset(slot_pages[0].data(), 0, PAGE_SIZE);
        std::memset(slot_pages[1].data(), 0, PAGE_SIZE);
    }
}

uint32_t CatalogManager::hash_table_name(const std::string& table_name) const {
    // Simple hash function: sum of characters modulo 3
    uint32_t hash = 0;
    for (char c : table_name) {
        hash += static_cast<uint32_t>(c);
    }
    return hash % 3;
}

int CatalogManager::find_slot(const std::string& table_name) const {
    // Search all slots: find_or_allocate_slot can place a table in any empty slot,
    // not only its primary hash slot, so we must scan all slots to find it.
    for (uint8_t i = 0; i < 3; ++i) {
        if (hash_index[i].is_valid && hash_index[i].table_name == table_name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

uint8_t CatalogManager::evict_lru_slot() {
    // Find hash slot with oldest last_access_time
    uint64_t oldest_time = UINT64_MAX;
    uint8_t lru_hash_slot = 0;

    for (uint8_t i = 0; i < 3; ++i) {
        if (hash_index[i].is_valid && hash_index[i].last_access_time < oldest_time) {
            oldest_time = hash_index[i].last_access_time;
            lru_hash_slot = i;
        }
    }

    // Temp cout: evict

    // If slot has dirty pages, flush first
    uint8_t pool_index = hash_index[lru_hash_slot].pool_slot_index;
    if (dirty_flags[pool_index][0] || dirty_flags[pool_index][1]) {
        write_slot_to_disk(hash_index[lru_hash_slot].db_path,
                          hash_index[lru_hash_slot].table_name,
                          pool_index);
        dirty_flags[pool_index][0] = false;
        dirty_flags[pool_index][1] = false;
    }

    // Mark slot as invalid
    hash_index[lru_hash_slot].is_valid = false;
    hash_index[lru_hash_slot].table_name = "";
    hash_index[lru_hash_slot].db_path = "";

    return lru_hash_slot;  // Return hash slot index, not pool index
}

uint8_t CatalogManager::find_or_allocate_slot(const std::string& table_name) {
    // First, check if table already exists
    int existing_slot = find_slot(table_name);
    if (existing_slot >= 0) {
        return static_cast<uint8_t>(existing_slot);
    }

    // Find empty hash slot
    for (uint8_t i = 0; i < 3; ++i) {
        if (!hash_index[i].is_valid) {
            return i;
        }
    }

    // No empty slot - evict LRU (returns pool slot index, but we need hash slot)
    // Actually, evict_lru_slot returns pool slot, but we need hash slot index.
    // Let's fix this: evict should return hash slot index, not pool slot.
    // For now, find the hash slot that uses the evicted pool slot.
    uint8_t evicted_pool = evict_lru_slot();
    
    // Find hash slot that was using this pool slot (should be the one we just evicted)
    for (uint8_t i = 0; i < 3; ++i) {
        if (!hash_index[i].is_valid) {
            return i;  // This should be the one we just evicted
        }
    }
    
    // Fallback: use hash slot 0
    return 0;
}

void CatalogManager::load_pages_to_slot(const std::string& db_path, const std::string& table_name, uint8_t pool_slot_index) {
    StorageManager sm(table_name, db_path);
    sm.read_page(0, catalog_pool[pool_slot_index][0].data());
    sm.read_page(1, catalog_pool[pool_slot_index][1].data());
}

void CatalogManager::write_slot_to_disk(const std::string& db_path, const std::string& table_name, uint8_t pool_slot_index) {
    StorageManager sm(table_name, db_path);
    if (dirty_flags[pool_slot_index][0]) {
        sm.write_page(0, catalog_pool[pool_slot_index][0].data());
        dirty_flags[pool_slot_index][0] = false;
    }
    if (dirty_flags[pool_slot_index][1]) {
        sm.write_page(1, catalog_pool[pool_slot_index][1].data());
        dirty_flags[pool_slot_index][1] = false;
    }
}

void CatalogManager::load_table_meta(const std::string& db_path, const std::string& table_name) {
    // Find or allocate a hash slot
    uint8_t hash_slot = find_or_allocate_slot(table_name);
    
    uint8_t pool_slot;
    
    // If slot was invalid, assign it a pool slot
    if (!hash_index[hash_slot].is_valid) {
        // Find an unused pool slot
        bool found = false;
        for (uint8_t i = 0; i < 3; ++i) {
            bool used = false;
            for (const auto& slot : hash_index) {
                if (slot.is_valid && slot.pool_slot_index == i) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                pool_slot = i;
                found = true;
                break;
            }
        }

        // If all pool slots are used, reuse the one from evicted slot (shouldn't happen)
        if (!found) {
            pool_slot = 0;  // Fallback
        }

        hash_index[hash_slot].pool_slot_index = pool_slot;
    } else {
        pool_slot = hash_index[hash_slot].pool_slot_index;
    }

    // Load pages 0 and 1 from disk into pool slot
    load_pages_to_slot(db_path, table_name, pool_slot);

    // Update hash index
    hash_index[hash_slot].table_name = table_name;
    hash_index[hash_slot].db_path = db_path;
    hash_index[hash_slot].is_valid = true;
    hash_index[hash_slot].last_access_time = ++access_counter;
    dirty_flags[pool_slot][0] = false;
    dirty_flags[pool_slot][1] = false;
}

uint8_t* CatalogManager::get_page0(const std::string& db_path, const std::string& table_name) {
    int slot = find_slot(table_name);
    if (slot >= 0) {
        hash_index[slot].last_access_time = ++access_counter;
        uint8_t pool_slot = hash_index[slot].pool_slot_index;
        return catalog_pool[pool_slot][0].data();
    }
    load_table_meta(db_path, table_name);
    slot = find_slot(table_name);
    if (slot < 0) {
        throw std::runtime_error("Failed to load table for page0: " + table_name);
    }
    hash_index[slot].last_access_time = ++access_counter;
    uint8_t pool_slot = hash_index[slot].pool_slot_index;
    return catalog_pool[pool_slot][0].data();
}

uint8_t* CatalogManager::get_table_meta(const std::string& db_path, const std::string& table_name) {
    // Check if already in pool
    int slot = find_slot(table_name);
    if (slot >= 0) {
        // Update LRU timestamp
        hash_index[slot].last_access_time = ++access_counter;
        uint8_t pool_slot = hash_index[slot].pool_slot_index;
        return catalog_pool[pool_slot][1].data();
    }

    // Not in pool - load it
    load_table_meta(db_path, table_name);
    slot = find_slot(table_name);
    if (slot < 0) {
        throw std::runtime_error("Failed to load table meta: " + table_name);
    }

    uint8_t pool_slot = hash_index[slot].pool_slot_index;
    return catalog_pool[pool_slot][1].data();
}

void CatalogManager::mark_page0_dirty(const std::string& db_path, const std::string& table_name) {
    int slot = find_slot(table_name);
    if (slot >= 0) {
        uint8_t pool_slot = hash_index[slot].pool_slot_index;
        dirty_flags[pool_slot][0] = true;
    }
}

void CatalogManager::create_table_meta(const std::string& db_path, const std::string& table_name, const CreateTableStmt& schema) {
    // Use StorageManager to create the table file with schema
    StorageManager manager = StorageManager::create(table_name, schema, db_path);

    // Now load the meta page into catalog pool
    load_table_meta(db_path, table_name);

    // Mark as dirty (since we just created it, it's already on disk, but mark dirty for consistency)
    int slot = find_slot(table_name);
    if (slot >= 0) {
        uint8_t pool_slot = hash_index[slot].pool_slot_index;
        dirty_flags[pool_slot][0] = false;
        dirty_flags[pool_slot][1] = false;
    }

    manager.close();
}

CreateTableStmt CatalogManager::read_schema(const std::string& db_path, const std::string& table_name) {
    // Get meta page (page 1) from pool (loads if not present)
    uint8_t* page1 = get_table_meta(db_path, table_name);

    // Verify page kind
    PageHeader* header = reinterpret_cast<PageHeader*>(page1);
    if (header->kind != static_cast<uint16_t>(PageKind::PAGE_META)) {
        throw std::runtime_error("Page 1 is not a META page for table: " + table_name);
    }
    if (header->page_id != 1) {
        throw std::runtime_error("Expected page 1 but got page " + std::to_string(header->page_id) + " for table: " + table_name);
    }

    // Read schema size
    uint16_t schema_size;
    std::memcpy(&schema_size, page1 + PAGE1_SCHEMA_SIZE_OFFSET, sizeof(uint16_t));

    if (schema_size == 0 || schema_size > PAGE1_MAX_SCHEMA_SIZE) {
        throw std::runtime_error("Invalid schema size in page 1 for table: " + table_name);
    }

    // Deserialize schema
    return deserialize_schema(page1 + PAGE1_SCHEMA_DATA_OFFSET, schema_size);
}

void CatalogManager::write_schema(const std::string& db_path, const std::string& table_name, const CreateTableStmt& schema) {
    // Get meta page (page 1) from pool (loads if not present)
    uint8_t* page1 = get_table_meta(db_path, table_name);

    // Serialize new schema
    std::vector<uint8_t> schema_data = serialize_schema(schema);
    if (schema_data.size() > PAGE1_MAX_SCHEMA_SIZE) {
        throw std::runtime_error("Schema too large for page 1");
    }

    // Clear old schema area
    std::memset(page1 + PAGE1_SCHEMA_SIZE_OFFSET, 0, PAGE1_MAX_SCHEMA_SIZE + sizeof(uint16_t));

    // Write new schema size
    uint16_t schema_size = static_cast<uint16_t>(schema_data.size());
    std::memcpy(page1 + PAGE1_SCHEMA_SIZE_OFFSET, &schema_size, sizeof(uint16_t));

    // Write new schema data
    std::memcpy(page1 + PAGE1_SCHEMA_DATA_OFFSET, schema_data.data(), schema_data.size());

    // Update free_start if needed
    PageHeader* header = reinterpret_cast<PageHeader*>(page1);
    uint16_t new_free_start = PAGE1_SCHEMA_DATA_OFFSET + schema_size;
    if (new_free_start > header->free_start) {
        header->free_start = new_free_start;
    }

    // Mark page 1 as dirty
    int slot = find_slot(table_name);
    if (slot >= 0) {
        uint8_t pool_slot = hash_index[slot].pool_slot_index;
        dirty_flags[pool_slot][1] = true;
    }
}

void CatalogManager::flush() {
    for (uint8_t i = 0; i < 3; ++i) {
        if (dirty_flags[i][0] || dirty_flags[i][1]) {
            for (const auto& slot : hash_index) {
                if (slot.is_valid && slot.pool_slot_index == i) {
                    write_slot_to_disk(slot.db_path, slot.table_name, i);
                    break;
                }
            }
        }
    }
}

void CatalogManager::clear() {
    // Flush dirty pages first
    flush();

    // Clear all slots
    for (auto& slot : hash_index) {
        slot.is_valid = false;
        slot.table_name = "";
        slot.db_path = "";
        slot.last_access_time = 0;
    }

    // Clear all dirty flags
    for (auto& slot_flags : dirty_flags) {
        slot_flags[0] = false;
        slot_flags[1] = false;
    }

    // Zero out catalog pool
    for (auto& slot_pages : catalog_pool) {
        std::memset(slot_pages[0].data(), 0, PAGE_SIZE);
        std::memset(slot_pages[1].data(), 0, PAGE_SIZE);
    }
}

std::vector<std::pair<std::string, std::string>> CatalogManager::list_cached_tables() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& slot : hash_index) {
        if (slot.is_valid) {
            result.push_back({slot.table_name, slot.db_path});
        }
    }
    return result;
}

std::vector<CatalogCacheEntry> CatalogManager::view_cache() const {
    std::vector<CatalogCacheEntry> result;
    for (uint8_t i = 0; i < 3; ++i) {
        const auto& slot = hash_index[i];
        if (!slot.is_valid) continue;
        CatalogCacheEntry e;
        e.slot = static_cast<int>(i);
        e.table_name = slot.table_name;
        e.db_path = slot.db_path;
        e.last_access_time = slot.last_access_time;
        e.dirty = dirty_flags[slot.pool_slot_index][0] || dirty_flags[slot.pool_slot_index][1];
        result.push_back(e);
    }
    return result;
}

void CatalogManager::evict_table(const std::string& table_name) {
    int slot = find_slot(table_name);
    if (slot < 0) return;
    uint8_t pool_index = hash_index[slot].pool_slot_index;
    if (dirty_flags[pool_index][0] || dirty_flags[pool_index][1]) {
        write_slot_to_disk(hash_index[slot].db_path, hash_index[slot].table_name, pool_index);
        dirty_flags[pool_index][0] = false;
        dirty_flags[pool_index][1] = false;
    }
    hash_index[slot].is_valid = false;
    hash_index[slot].table_name = "";
    hash_index[slot].db_path = "";
}

uint64_t CatalogManager::get_and_increment_row_id(const std::string& db_path, const std::string& table_name) {
    uint8_t* page0 = get_page0(db_path, table_name);
    uint64_t current_row_id = get_next_row_id(page0);
    set_next_row_id(page0, current_row_id + 1);
    mark_page0_dirty(db_path, table_name);
    return current_row_id;
}

uint64_t CatalogManager::get_and_increment_auto_increment(const std::string& db_path, const std::string& table_name, uint16_t column_index) {
    if (column_index >= PAGE0_AI_COUNTER_COUNT) {
        throw std::runtime_error("AUTO_INCREMENT column index out of range");
    }
    uint8_t* page0 = get_page0(db_path, table_name);
    uint64_t current = get_auto_increment_counter(page0, column_index);
    set_auto_increment_counter(page0, column_index, current + 1);
    mark_page0_dirty(db_path, table_name);
    return current;
}
