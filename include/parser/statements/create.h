#ifndef CREATE_H
#define CREATE_H

#include <vector>
#include <string>
#include <stdexcept>
#include <variant>

/*
 * Example SQL statements:
 * 
 * CREATE DATABASE mydb;
 *   - Creates a new database named "mydb"
 * 
 * CREATE TABLE users (
 *     id INT PRIMARY KEY,
 *     name VARCHAR(255) NOT NULL,
 *     email VARCHAR(255) UNIQUE
 * );
 *   - Creates a table named "users"
 *   - With columns: id (INT, PRIMARY KEY), name (VARCHAR(255), NOT NULL), email (VARCHAR(255), UNIQUE)
 */

// Column definition structure
// Represents a single column in a CREATE TABLE statement
struct ColumnDef {
    std::string name;              // Column name (e.g., "id", "name", "email")
    std::string data_type;         // Data type (e.g., "INT", "VARCHAR(255)", "BIGINT")
    bool is_primary_key = false;   // True if column has PRIMARY KEY constraint
    bool is_unique = false;        // True if column has UNIQUE constraint
    bool is_not_null = false;      // True if column has NOT NULL constraint
    bool is_auto_increment = false;  // True if column has AUTO_INCREMENT
};

// CREATE DATABASE statement structure
// Example: CREATE DATABASE mydb;
struct CreateDatabaseStmt {
    std::string database_name;     // Name of the database to create
};

// CREATE TABLE statement structure
// Example: CREATE TABLE users (id INT, name VARCHAR(255));
struct CreateTableStmt {
    std::string table_name;        // Name of the table to create
    std::vector<ColumnDef> columns; // List of column definitions
};

// CREATE statement variant (can be either DATABASE or TABLE)
struct CreateStmt {
private:
    std::variant<CreateDatabaseStmt, CreateTableStmt> data;

public:
    // Constructor for CREATE DATABASE
    CreateStmt(CreateDatabaseStmt db_stmt) : data(db_stmt) {}

    // Constructor for CREATE TABLE
    CreateStmt(CreateTableStmt table_stmt) : data(table_stmt) {}

    // Copy constructor
    CreateStmt(const CreateStmt& other) = default;

    // Assignment operator
    CreateStmt& operator=(const CreateStmt& other) = default;

    // Destructor
    ~CreateStmt() = default;

    // Check if it's a CREATE DATABASE
    bool is_database() const {
        return std::holds_alternative<CreateDatabaseStmt>(data);
    }

    // Check if it's a CREATE TABLE
    bool is_table() const {
        return std::holds_alternative<CreateTableStmt>(data);
    }

    // Access CREATE DATABASE statement
    CreateDatabaseStmt& as_database() {
        if (!std::holds_alternative<CreateDatabaseStmt>(data)) {
            throw std::runtime_error("CreateStmt is not a CREATE DATABASE statement");
        }
        return std::get<CreateDatabaseStmt>(data);
    }

    const CreateDatabaseStmt& as_database() const {
        if (!std::holds_alternative<CreateDatabaseStmt>(data)) {
            throw std::runtime_error("CreateStmt is not a CREATE DATABASE statement");
        }
        return std::get<CreateDatabaseStmt>(data);
    }

    // Access CREATE TABLE statement
    CreateTableStmt& as_table() {
        if (!std::holds_alternative<CreateTableStmt>(data)) {
            throw std::runtime_error("CreateStmt is not a CREATE TABLE statement");
        }
        return std::get<CreateTableStmt>(data);
    }

    const CreateTableStmt& as_table() const {
        if (!std::holds_alternative<CreateTableStmt>(data)) {
            throw std::runtime_error("CreateStmt is not a CREATE TABLE statement");
        }
        return std::get<CreateTableStmt>(data);
    }
};

// Forward declaration
class Parser;

// Function declarations for parsing CREATE statements
ColumnDef parse_column_def(Parser& parser);
CreateDatabaseStmt parse_create_database(Parser& parser);
CreateTableStmt parse_create_table(Parser& parser);
CreateStmt parse_create(Parser& parser);

#endif // CREATE_H
