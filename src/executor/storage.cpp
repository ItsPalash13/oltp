#include "executor/storage.h"
#include "storage/bufferpool_manager.h"
#include "storage/bplustree.h"
#include "storage/catalog_manager.h"
#include <stdexcept>
#include <filesystem>
#include <fstream>

Storage::Storage(const std::string& db_path) : database_path(db_path), next_table_id_(1) {
    std::filesystem::create_directories(db_path);
    catalog_ = std::make_unique<CatalogManager>();
}

Storage::~Storage() = default;

void Storage::create_table(const CreateTableStmt& schema) {
    std::string table_name = schema.table_name;
    
    // Check if table already exists
    if (has_table(table_name)) {
        throw std::runtime_error("Table already exists: " + table_name);
    }
    
    // Create storage manager with schema in page 0
    // Note: StorageManager::create returns by value, but we can't copy it (contains fstream)
    // We need to move-construct it. Since make_unique tries to copy, we use new directly.
    StorageManager temp = StorageManager::create(table_name, schema, database_path);
    auto storage_manager = std::unique_ptr<StorageManager>(new StorageManager(std::move(temp)));
    
    // Store storage manager handle
    storage_managers[table_name] = std::move(storage_manager);
    table_name_to_id_[table_name] = next_table_id_;
    table_id_to_name_[next_table_id_] = table_name;
    next_table_id_++;

    // Initialize empty in-memory table for backward compatibility
    tables[table_name] = Table();
}

CreateTableStmt Storage::get_table_schema(const std::string& name) {
    StorageManager& manager = get_or_load_storage_manager(name);
    return manager.read_schema();
}

StorageManager& Storage::get_or_load_storage_manager(const std::string& name) {
    // Check if already loaded
    auto it = storage_managers.find(name);
    if (it != storage_managers.end()) {
        return *(it->second);
    }
    
    // Load storage manager
    if (!has_table(name)) {
        throw std::runtime_error("Table does not exist: " + name);
    }
    
    auto storage_manager = std::make_unique<StorageManager>(name, database_path);
    StorageManager& ref = *storage_manager;
    storage_managers[name] = std::move(storage_manager);
    if (table_name_to_id_.find(name) == table_name_to_id_.end()) {
        table_name_to_id_[name] = next_table_id_;
        table_id_to_name_[next_table_id_] = name;
        next_table_id_++;
    }

    return ref;
}

StorageManager& Storage::get_storage_manager(const std::string& name) {
    return get_or_load_storage_manager(name);
}

BufferPoolManager& Storage::get_buffer_pool() {
    if (!buffer_pool_) {
        buffer_pool_ = std::make_unique<BufferPoolManager>(this, BufferPoolManager::DEFAULT_NUM_FRAMES);
    }
    return *buffer_pool_;
}

CatalogManager& Storage::get_catalog() {
    return *catalog_;
}

uint32_t Storage::get_table_id(const std::string& table_name) {
    auto it = table_name_to_id_.find(table_name);
    if (it == table_name_to_id_.end()) {
        throw std::runtime_error("Table not loaded: " + table_name);
    }
    return it->second;
}

StorageManager& Storage::get_storage_manager_by_id(uint32_t table_id) {
    auto it = table_id_to_name_.find(table_id);
    if (it == table_id_to_name_.end()) {
        throw std::runtime_error("Invalid table_id: " + std::to_string(table_id));
    }
    return get_storage_manager(it->second);
}

Table& Storage::get_table(const std::string& name) {
    // For backward compatibility, return in-memory table
    // If table doesn't exist in memory but exists on disk, create empty in-memory table
    if (tables.find(name) == tables.end()) {
        if (has_table(name)) {
            // Table exists on disk, create empty in-memory table
        tables[name] = Table();
        } else {
            throw std::runtime_error("Table does not exist: " + name);
        }
    }
    return tables[name];
}

void Storage::insert(const std::string& table, const Tuple& tuple) {
    // For backward compatibility, insert into in-memory table
    // TODO: Later, this should insert into page-based storage
    get_table(table).push_back(tuple);
}

bool Storage::has_table(const std::string& name) const {
    // Check if .ibd file exists
    std::string filename = database_path + name + ".ibd";
    return std::filesystem::exists(filename);
}

std::vector<std::string> Storage::list_tables() const {
    std::vector<std::string> result;
    
    if (!std::filesystem::exists(database_path)) {
        return result;
    }
    
    // Scan directory for .ibd files
    for (const auto& entry : std::filesystem::directory_iterator(database_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ibd") {
            std::string filename = entry.path().filename().string();
            // Remove .ibd extension
            std::string table_name = filename.substr(0, filename.size() - 4);
            result.push_back(table_name);
        }
    }
    
    return result;
}

std::unique_ptr<BPlusTree> Storage::create_bplustree(const std::string& table_name) {
    if (!has_table(table_name)) {
        throw std::runtime_error("Table does not exist: " + table_name);
    }
    BufferPoolManager& bp = get_buffer_pool();
    StorageManager& sm = get_storage_manager(table_name);
    uint32_t tid = get_table_id(table_name);
    return std::make_unique<BPlusTree>(get_catalog(), bp, sm, database_path, table_name, tid);
}
