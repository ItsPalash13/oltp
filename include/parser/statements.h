#ifndef STATEMENTS_H
#define STATEMENTS_H

#include <vector>
#include <string>
#include <stdexcept>
#include <variant>

// Forward declarations
struct Expr;
struct Token;
enum class TokenType;
class Parser;

// Statement type enum
enum class StatementType {
    Select,
    Create
};

// Column definition structure
struct ColumnDef {
    std::string name;
    std::string data_type;
    bool is_primary_key = false;
    bool is_unique = false;
    bool is_not_null = false;
};

// Select statement structure
struct SelectStmt {
    std::vector<Expr*> columns;
    std::string table;
    Expr* where = nullptr;
    std::vector<Expr*> order_by;  // Optional
    std::vector<Expr*> group_by;  // Optional
};

// CREATE DATABASE statement structure
struct CreateDatabaseStmt {
    std::string database_name;
};

// CREATE TABLE statement structure
struct CreateTableStmt {
    std::string table_name;
    std::vector<ColumnDef> columns;
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

// Statement abstraction class using std::variant
class Statement {
private:
    std::variant<SelectStmt, CreateStmt> data;

public:
    // Constructor for Select statement
    Statement(SelectStmt stmt) : data(stmt) {}

    // Constructor for Create statement
    Statement(CreateStmt stmt) : data(stmt) {}

    // Copy constructor (default is fine with std::variant)
    Statement(const Statement& other) = default;

    // Assignment operator (default is fine with std::variant)
    Statement& operator=(const Statement& other) = default;

    // Destructor (default is fine with std::variant)
    ~Statement() = default;

    // Get statement type
    StatementType get_type() const {
        if (std::holds_alternative<SelectStmt>(data)) {
            return StatementType::Select;
        }
        if (std::holds_alternative<CreateStmt>(data)) {
            return StatementType::Create;
        }
        throw std::runtime_error("Unknown statement type");
    }

    // Access SELECT statement
    SelectStmt& as_select() {
        if (!std::holds_alternative<SelectStmt>(data)) {
            throw std::runtime_error("Statement is not a SELECT statement");
        }
        return std::get<SelectStmt>(data);
    }

    const SelectStmt& as_select() const {
        if (!std::holds_alternative<SelectStmt>(data)) {
            throw std::runtime_error("Statement is not a SELECT statement");
        }
        return std::get<SelectStmt>(data);
    }

    // Access CREATE statement
    CreateStmt& as_create() {
        if (!std::holds_alternative<CreateStmt>(data)) {
            throw std::runtime_error("Statement is not a CREATE statement");
        }
        return std::get<CreateStmt>(data);
    }

    const CreateStmt& as_create() const {
        if (!std::holds_alternative<CreateStmt>(data)) {
            throw std::runtime_error("Statement is not a CREATE statement");
        }
        return std::get<CreateStmt>(data);
    }
};

// Function to parse SELECT statement (returns SelectStmt, will be wrapped in Statement)
SelectStmt parse_select(Parser& parser);

#endif // STATEMENTS_H
