#include "storage/db_manager.h"
#include "executor/storage.h"
#include <filesystem>
#include <stdexcept>
#include <algorithm>

DatabaseManager::DatabaseManager(const std::string& root_path)
    : root_path(root_path), current_db(""), storage_engine_(nullptr) {
    // Ensure root directory exists
    std::filesystem::create_directories(root_path);
}

void DatabaseManager::create_db(const std::string& db_name) {
    if (db_name.empty()) {
        throw std::runtime_error("Database name cannot be empty");
    }

    std::string db_path = root_path + db_name + "/";

    // Check if database already exists
    if (std::filesystem::exists(db_path)) {
        throw std::runtime_error("Database already exists: " + db_name);
    }

    // Create database directory
    if (!std::filesystem::create_directories(db_path)) {
        throw std::runtime_error("Failed to create database directory: " + db_path);
    }
}

void DatabaseManager::drop_db(const std::string& db_name) {
    if (db_name.empty()) {
        throw std::runtime_error("Database name cannot be empty");
    }

    std::string db_path = root_path + db_name + "/";

    // Check if database exists
    if (!std::filesystem::exists(db_path)) {
        throw std::runtime_error("Database does not exist: " + db_name);
    }

    // Remove database directory and all contents
    std::error_code ec;
    std::filesystem::remove_all(db_path, ec);
    if (ec) {
        throw std::runtime_error("Failed to drop database: " + db_name + " - " + ec.message());
    }

    // If dropped database was current, clear current_db and destroy storage engine
    if (current_db == db_name) {
        storage_engine_.reset();
        current_db = "";
    }
}

DatabaseManager::~DatabaseManager() = default;

void DatabaseManager::clear_current_db() {
    current_db = "";
    storage_engine_.reset();
}

std::string DatabaseManager::use_db(const std::string& db_name) {
    if (db_name.empty()) {
        throw std::runtime_error("Database name cannot be empty");
    }

    std::string db_path = root_path + db_name + "/";

    // Check if database exists
    if (!std::filesystem::exists(db_path)) {
        throw std::runtime_error("Database does not exist: " + db_name);
    }

    // Set current database and create/replace storage engine for this path
    current_db = db_name;
    storage_engine_ = std::make_unique<Storage>(db_path);
    return db_path;
}

Storage* DatabaseManager::get_storage_engine() {
    return storage_engine_.get();
}

std::string DatabaseManager::get_current_db_path() const {
    if (current_db.empty()) {
        return "";
    }
    return root_path + current_db + "/";
}

std::vector<std::string> DatabaseManager::list_databases() const {
    std::vector<std::string> databases;

    if (!std::filesystem::exists(root_path)) {
        return databases;
    }

    // Iterate through directories in root_path
    for (const auto& entry : std::filesystem::directory_iterator(root_path)) {
        if (entry.is_directory()) {
            std::string db_name = entry.path().filename().string();
            // Skip hidden directories and special entries
            if (!db_name.empty() && db_name[0] != '.') {
                databases.push_back(db_name);
            }
        }
    }

    // Sort for consistent output
    std::sort(databases.begin(), databases.end());
    return databases;
}

bool DatabaseManager::database_exists(const std::string& db_name) const {
    if (db_name.empty()) {
        return false;
    }
    std::string db_path = root_path + db_name + "/";
    return std::filesystem::exists(db_path) && std::filesystem::is_directory(db_path);
}
