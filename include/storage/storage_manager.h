#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "storage/page/page.h"
#include "parser/statements/create.h"
#include <string>
#include <fstream>
#include <memory>

/**
 * StorageManager - manages individual table files (.ibd).
 * 
 * Note: For database-level operations (create_db, drop_db, use_db),
 * use DatabaseManager. StorageManager works with paths provided by
 * DatabaseManager (e.g., "@data/mydb/").
 */
class StorageManager {
private:
    std::string filename;  // e.g., "users.ibd"
    std::fstream file;
    bool file_is_open;
    
    // Read page 0 (table header) into buffer
    void read_page0(uint8_t* buffer);
    
    // Write page 0 (table header) from buffer
    void write_page0(const uint8_t* buffer);
    
    // Read page 1 (meta/schema) into buffer
    void read_page1(uint8_t* buffer);
    
    // Write page 1 (meta/schema) from buffer
    void write_page1(const uint8_t* buffer);

public:
    // Open existing table file
    // @param db_path Database path (e.g., "@data/mydb/" from DatabaseManager::get_current_db_path())
    explicit StorageManager(const std::string& table_name, const std::string& db_path = "./data/");
    
    // Move constructor
    StorageManager(StorageManager&& other) noexcept;
    
    // Move assignment operator
    StorageManager& operator=(StorageManager&& other) noexcept;
    
    // Create new table file with schema
    // @param db_path Database path (e.g., "@data/mydb/" from DatabaseManager::get_current_db_path())
    static StorageManager create(const std::string& table_name, 
                                const CreateTableStmt& schema,
                                const std::string& db_path = "./data/");
    
    // Read schema from page 1 (meta page)
    CreateTableStmt read_schema();
    
    // Update schema in page 1 (meta page, for ALTER TABLE)
    void write_schema(const CreateTableStmt& schema);
    
    // Read a page from file
    void read_page(uint32_t page_id, uint8_t* buffer);
    
    // Write a page to file
    void write_page(uint32_t page_id, const uint8_t* buffer);
    
    // Get number of pages in file (file size / PAGE_SIZE)
    uint32_t get_page_count();
    
    // Get filename
    const std::string& get_filename() const { return filename; }
    
    // Check if file is open
    bool is_open() const { return file_is_open; }
    
    // Close file
    void close();
    
    ~StorageManager();
};

#endif // STORAGE_MANAGER_H
