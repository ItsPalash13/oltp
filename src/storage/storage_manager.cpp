#include "storage/storage_manager.h"
#include "storage/schema_serializer.h"
#include "storage/page/page.h"
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <string>
#include <algorithm>

StorageManager::StorageManager(const std::string& table_name, const std::string& db_path)
    : filename(db_path + table_name + ".ibd"), file_is_open(false) {
    
    // Ensure directory exists
    std::filesystem::create_directories(db_path);
    
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open table file: " + filename);
    }
    file_is_open = true;
}

// Helper function to extract database name from db_path (e.g., "@data/mydb/" -> "mydb")
static std::string extract_db_name(const std::string& db_path) {
    std::string path = db_path;
    // Remove trailing slashes
    while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    // Find last segment
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos && last_slash + 1 < path.length()) {
        return path.substr(last_slash + 1);
    }
    // If no slash found, return the path as-is (or empty)
    return path.empty() ? "default" : path;
}

StorageManager StorageManager::create(const std::string& table_name, 
                                       const CreateTableStmt& schema,
                                       const std::string& db_path) {
    std::string filename = db_path + table_name + ".ibd";
    
    // Check if file already exists
    if (std::filesystem::exists(filename)) {
        throw std::runtime_error("Table file already exists: " + filename);
    }
    
    // Ensure directory exists
    std::filesystem::create_directories(db_path);
    
    // Create and open file
    std::fstream file(filename, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create table file: " + filename);
    }
    
    // ===== PAGE 0: Table Header =====
    uint8_t page0[PAGE_SIZE];
    std::memset(page0, 0, PAGE_SIZE);
    
    // Write PageHeader for page 0
    PageHeader* header0 = reinterpret_cast<PageHeader*>(page0);
    header0->page_id = 0;
    header0->kind = static_cast<uint16_t>(PageKind::PAGE_HEADER);
    header0->level = static_cast<uint16_t>(PageLevel::PAGE_LEAF);
    header0->flags = 0;
    header0->cell_count = 0;
    header0->free_start = PAGE0_FREE_LIST_OFFSET;  // Start after fixed header + AI counters
    header0->free_end = PAGE_SIZE;
    header0->parent_page = 0;
    header0->lsn = 0;
    
    // Write root_page_id (B+ tree root; INVALID until first insert)
    const uint32_t root_page_id = ROOT_PAGE_ID_INVALID;
    std::memcpy(page0 + PAGE0_ROOT_PAGE_ID_OFFSET, &root_page_id, sizeof(uint32_t));
    
    // Write next_row_id (initially 1)
    const uint64_t next_row_id = 1;
    std::memcpy(page0 + PAGE0_NEXT_ROW_ID_OFFSET, &next_row_id, sizeof(uint64_t));
    
    // Write initial AUTO_INCREMENT counters (1) for each AUTO_INCREMENT column in schema
    uint16_t ai_count = 0;
    for (const auto& col : schema.columns) {
        if (col.is_auto_increment && ai_count < PAGE0_AI_COUNTER_COUNT) {
            const uint64_t initial_ai = 1;
            set_auto_increment_counter(page0, ai_count, initial_ai);
            ai_count++;
        }
    }
    
    // Extract database name from db_path
    std::string db_name = extract_db_name(db_path);
    
    // Write table_name (length-prefixed, max 256 bytes)
    uint16_t table_name_len = static_cast<uint16_t>(std::min(table_name.length(), static_cast<size_t>(PAGE0_TABLE_NAME_MAX_LEN)));
    std::memcpy(page0 + PAGE0_TABLE_NAME_LEN_OFFSET, &table_name_len, sizeof(uint16_t));
    std::memcpy(page0 + PAGE0_TABLE_NAME_OFFSET, table_name.c_str(), table_name_len);
    
    // Write database_name (length-prefixed, max 256 bytes)
    uint16_t db_name_len = static_cast<uint16_t>(std::min(db_name.length(), static_cast<size_t>(PAGE0_DB_NAME_MAX_LEN)));
    std::memcpy(page0 + PAGE0_DB_NAME_LEN_OFFSET, &db_name_len, sizeof(uint16_t));
    std::memcpy(page0 + PAGE0_DB_NAME_OFFSET, db_name.c_str(), db_name_len);
    
    // Write free_page_count (initially 0, or 1 with page 2 as first free data page)
    uint32_t free_page_count = 0;  // Start with empty free list
    std::memcpy(page0 + PAGE0_FREE_COUNT_OFFSET, &free_page_count, sizeof(uint32_t));
    // free_pages[] array starts at PAGE0_FREE_LIST_OFFSET, but is empty initially
    
    // Write page 0 to file
    file.seekp(0);
    file.write(reinterpret_cast<const char*>(page0), PAGE_SIZE);
    
    // ===== PAGE 1: Meta/Schema =====
    uint8_t page1[PAGE_SIZE];
    std::memset(page1, 0, PAGE_SIZE);
    
    // Write PageHeader for page 1
    PageHeader* header1 = reinterpret_cast<PageHeader*>(page1);
    header1->page_id = 1;
    header1->kind = static_cast<uint16_t>(PageKind::PAGE_META);
    header1->level = static_cast<uint16_t>(PageLevel::PAGE_LEAF);
    header1->flags = 0;
    header1->cell_count = 0;  // No slots for schema
    header1->free_start = PAGE1_SCHEMA_DATA_OFFSET;  // Start after schema
    header1->free_end = PAGE_SIZE;  // No slot directory
    header1->parent_page = 0;
    header1->lsn = 0;
    
    // Serialize schema
    std::vector<uint8_t> schema_data = serialize_schema(schema);
    if (schema_data.size() > PAGE1_MAX_SCHEMA_SIZE) {
        throw std::runtime_error("Schema too large for page 1");
    }
    
    // Write schema size (2 bytes)
    uint16_t schema_size = static_cast<uint16_t>(schema_data.size());
    std::memcpy(page1 + PAGE1_SCHEMA_SIZE_OFFSET, &schema_size, sizeof(uint16_t));
    
    // Write schema data
    std::memcpy(page1 + PAGE1_SCHEMA_DATA_OFFSET, schema_data.data(), schema_data.size());
    
    // Update free_start to point after schema
    header1->free_start = PAGE1_SCHEMA_DATA_OFFSET + schema_size;
    
    // Write page 1 to file
    file.seekp(PAGE_SIZE);
    file.write(reinterpret_cast<const char*>(page1), PAGE_SIZE);
    file.close();
    
    // Return opened StorageManager
    return StorageManager(table_name, db_path);
}

// Move constructor
StorageManager::StorageManager(StorageManager&& other) noexcept
    : filename(std::move(other.filename)),
      file_is_open(other.file_is_open) {
    // Move the file stream - we need to close the old one and open the new one
    // since fstream doesn't have a move constructor
    if (other.file_is_open) {
        other.file.close();
        other.file_is_open = false;
    }
    // Open the file with the moved filename
    if (!filename.empty()) {
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        file_is_open = file.is_open();
    } else {
        file_is_open = false;
    }
}

// Move assignment operator
StorageManager& StorageManager::operator=(StorageManager&& other) noexcept {
    if (this != &other) {
        // Close current file if open
        if (file_is_open) {
            file.close();
        }
        
        // Move members
        filename = std::move(other.filename);
        file_is_open = other.file_is_open;
        
        // Close other's file and open ours
        if (other.file_is_open) {
            other.file.close();
            other.file_is_open = false;
        }
        
        // Open the file with the moved filename
        if (!filename.empty()) {
            file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
            file_is_open = file.is_open();
        } else {
            file_is_open = false;
        }
    }
    return *this;
}

CreateTableStmt StorageManager::read_schema() {
    if (!file_is_open) {
        throw std::runtime_error("Table file not open");
    }
    
    uint8_t page1[PAGE_SIZE];
    read_page(1, page1);
    
    // Verify page kind
    PageHeader* header = reinterpret_cast<PageHeader*>(page1);
    if (header->kind != static_cast<uint16_t>(PageKind::PAGE_META)) {
        throw std::runtime_error("Page 1 is not a META page");
    }
    
    // Read schema size
    uint16_t schema_size;
    std::memcpy(&schema_size, page1 + PAGE1_SCHEMA_SIZE_OFFSET, sizeof(uint16_t));
    
    if (schema_size == 0 || schema_size > PAGE1_MAX_SCHEMA_SIZE) {
        throw std::runtime_error("Invalid schema size in page 1");
    }
    
    // Deserialize schema
    return deserialize_schema(page1 + PAGE1_SCHEMA_DATA_OFFSET, schema_size);
}

void StorageManager::write_schema(const CreateTableStmt& schema) {
    if (!file_is_open) {
        throw std::runtime_error("Table file not open");
    }
    
    uint8_t page1[PAGE_SIZE];
    read_page(1, page1);
    
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
    
    write_page(1, page1);
}

void StorageManager::read_page(uint32_t page_id, uint8_t* buffer) {
    if (!file_is_open) {
        throw std::runtime_error("Table file not open");
    }
    
    size_t offset = page_id * PAGE_SIZE;
    file.seekg(offset);
    file.read(reinterpret_cast<char*>(buffer), PAGE_SIZE);
    
    if (file.fail()) {
        throw std::runtime_error("Failed to read page " + std::to_string(page_id));
    }
}

void StorageManager::write_page(uint32_t page_id, const uint8_t* buffer) {
    if (!file_is_open) {
        throw std::runtime_error("Table file not open");
    }
    
    size_t offset = page_id * PAGE_SIZE;
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(buffer), PAGE_SIZE);
    file.flush();
    
    if (file.fail()) {
        throw std::runtime_error("Failed to write page " + std::to_string(page_id));
    }
}

uint32_t StorageManager::get_page_count() {
    if (!file_is_open) {
        throw std::runtime_error("Table file not open");
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    return static_cast<uint32_t>(size / PAGE_SIZE);
}

void StorageManager::read_page0(uint8_t* buffer) {
    read_page(0, buffer);
}

void StorageManager::write_page0(const uint8_t* buffer) {
    write_page(0, buffer);
}

void StorageManager::read_page1(uint8_t* buffer) {
    read_page(1, buffer);
}

void StorageManager::write_page1(const uint8_t* buffer) {
    write_page(1, buffer);
}

void StorageManager::close() {
    if (file_is_open) {
        file.close();
        file_is_open = false;
    }
}

StorageManager::~StorageManager() {
    close();
}
