#ifndef UPDATE_H
#define UPDATE_H

#include <vector>
#include <string>

// Forward declarations
struct Expr;

/*
 * Example SQL statements:
 * 
 * UPDATE users SET name = 'John', email = 'john@example.com' WHERE id = 1;
 *   - Updates specific columns with new values based on WHERE condition
 * 
 * UPDATE products SET price = price * 0.9 WHERE price > 100;
 *   - Updates with expression in assignment
 */

// Assignment structure (column = value)
struct Assignment {
    std::string column;  // Column name
    Expr* value;         // Expression value
};

// UPDATE statement structure
struct UpdateStmt {
    std::string table;                      // Table name
    std::vector<Assignment> assignments;    // Column = value pairs
    Expr* where = nullptr;                  // Optional WHERE clause
};

// Forward declaration
class Parser;

// Function to parse UPDATE statement
UpdateStmt parse_update(Parser& parser);

#endif // UPDATE_H
