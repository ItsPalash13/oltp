#ifndef STORAGE_H
#define STORAGE_H

#include "executor/types.h"
#include "storage/storage_manager.h"
#include "storage/bufferpool_manager.h"
#include "parser/statements/create.h"
#include <map>
#include <string>
#include <memory>
#include <vector>

class CatalogManager;

// Storage class that manages table files and in-memory data
class Storage {
private:
    std::map<std::string, Table> tables;  // In-memory tables (for backward compatibility)
    std::map<std::string, std::unique_ptr<StorageManager>> storage_managers;  // Storage manager handles
    std::unique_ptr<BufferPoolManager> buffer_pool_;  // Single shared buffer pool (pages 2+)
    std::unique_ptr<CatalogManager> catalog_;  // Catalog cache (pages 0/1) for this db
    std::string database_path;
    uint32_t next_table_id_;
    std::map<std::string, uint32_t> table_name_to_id_;
    std::map<uint32_t, std::string> table_id_to_name_;

    // Load storage manager if not already loaded
    StorageManager& get_or_load_storage_manager(const std::string& name);

public:
    BufferPoolManager& get_buffer_pool();
    CatalogManager& get_catalog();
    // Constructor: initialize with database path
    explicit Storage(const std::string& db_path = "./data/");
    ~Storage();
    
    // Create table (creates .ibd file with schema in page 0)
    void create_table(const CreateTableStmt& schema);
    
    // Get table schema from file
    CreateTableStmt get_table_schema(const std::string& name);
    
    // Get reference to table data (in-memory, for backward compatibility)
    Table& get_table(const std::string& name);
    
    // Insert a tuple into a table (in-memory, for backward compatibility)
    void insert(const std::string& table, const Tuple& tuple);
    
    // Check if table exists (checks for .ibd file)
    bool has_table(const std::string& name) const;
    
    // List all tables (scan directory for .ibd files)
    std::vector<std::string> list_tables() const;
    
    // Get storage manager for direct page operations
    StorageManager& get_storage_manager(const std::string& name);

    // Table ID for shared buffer pool (stable per table for process lifetime)
    uint32_t get_table_id(const std::string& table_name);

    // Resolve table_id to StorageManager (used by BufferPoolManager for I/O)
    StorageManager& get_storage_manager_by_id(uint32_t table_id);

    // Create B+ tree for a table (uses internal catalog)
    std::unique_ptr<class BPlusTree> create_bplustree(const std::string& table_name);
};

#endif // STORAGE_H
