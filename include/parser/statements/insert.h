#ifndef INSERT_H
#define INSERT_H

#include <vector>
#include <string>

// Forward declarations
struct Expr;

/*
 * Example SQL statements:
 * 
 * INSERT INTO users (id, name, email) VALUES (1, 'John', 'john@example.com');
 *   - Inserts values into specific columns
 * 
 * INSERT INTO users VALUES (1, 'John', 'john@example.com');
 *   - Inserts values into all columns (column list is optional)
 */

// INSERT statement structure
struct InsertStmt {
    std::string table;                    // Table name
    std::vector<std::string> columns;     // Optional column list
    std::vector<Expr*> values;            // Values to insert
};

// Forward declaration
class Parser;

// Function to parse INSERT statement
InsertStmt parse_insert(Parser& parser);

#endif // INSERT_H
