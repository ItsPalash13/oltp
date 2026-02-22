#ifndef DELETE_H
#define DELETE_H

#include <string>

// Forward declarations
struct Expr;

/*
 * Example SQL statements:
 * 
 * DELETE FROM users WHERE id = 1;
 *   - Deletes rows matching the WHERE condition
 * 
 * DELETE FROM products;
 *   - Deletes all rows from the table (WHERE clause is optional)
 */

// DELETE statement structure
struct DeleteStmt {
    std::string table;      // Table name
    Expr* where = nullptr;  // Optional WHERE clause
};

// Forward declaration
class Parser;

// Function to parse DELETE statement
DeleteStmt parse_delete(Parser& parser);

#endif // DELETE_H
