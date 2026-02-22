#ifndef SELECT_H
#define SELECT_H

#include <vector>
#include <string>

// Forward declarations
struct Expr;

// Select statement structure
struct SelectStmt {
    std::vector<Expr*> columns;
    std::string table;
    Expr* where = nullptr;
    std::vector<Expr*> order_by;  // Optional
    std::vector<Expr*> group_by;  // Optional
};

// Forward declaration
class Parser;

// Function to parse SELECT statement
SelectStmt parse_select(Parser& parser);

#endif // SELECT_H
