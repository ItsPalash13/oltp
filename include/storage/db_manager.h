#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <string>
#include <vector>
#include <memory>

class Storage;

/**
 * Database Manager - handles database-level operations.
 * All databases are stored under @data/ (project root).
 * Structure: @data/<db_name>/
 */
class DatabaseManager {
private:
    std::string current_db;  // Current database name (empty if none selected)
    std::string root_path;   // Root data path: "@data/"
    std::unique_ptr<Storage> storage_engine_;  // Created on use_db, cleared on drop_db of current

public:
    /**
     * Constructor - initialize with root data path.
     * @param root_path Root directory for all databases (default: "@data/")
     */
    explicit DatabaseManager(const std::string& root_path = "@data/");

    ~DatabaseManager();

    /**
     * Create a new database.
     * Creates directory at @data/<db_name>/
     * @param db_name Name of the database to create
     * @throws std::runtime_error if database already exists or creation fails
     */
    void create_db(const std::string& db_name);

    /**
     * Drop (delete) a database.
     * Removes directory @data/<db_name>/ and all its contents.
     * If the dropped database is the current one, clears current_db.
     * @param db_name Name of the database to drop
     * @throws std::runtime_error if database doesn't exist or deletion fails
     */
    void drop_db(const std::string& db_name);

    /**
     * Clear current database context (no database selected).
     * Used when switching to a connection with no database selected.
     */
    void clear_current_db();

    /**
     * Use (switch to) a database.
     * Sets current database context and creates/replaces the storage engine for that path.
     * @param db_name Name of the database to use
     * @return Full path to database: @data/<db_name>/
     * @throws std::runtime_error if database doesn't exist
     */
    std::string use_db(const std::string& db_name);

    /**
     * Get the storage engine for the current database.
     * @return Pointer to Storage, or nullptr if no database selected
     */
    Storage* get_storage_engine();

    /**
     * Get the current database path.
     * @return Full path to current database: @data/<current_db>/ or empty if none selected
     */
    std::string get_current_db_path() const;

    /**
     * Get the current database name.
     * @return Current database name or empty string if none selected
     */
    std::string get_current_db() const { return current_db; }

    /**
     * List all databases in @data/.
     * @return Vector of database names
     */
    std::vector<std::string> list_databases() const;

    /**
     * Check if a database exists.
     * @param db_name Name of the database to check
     * @return True if database exists, false otherwise
     */
    bool database_exists(const std::string& db_name) const;

    /**
     * Get the root data path.
     * @return Root path (e.g., "@data/")
     */
    const std::string& get_root_path() const { return root_path; }
};

#endif // DB_MANAGER_H
