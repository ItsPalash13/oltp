#ifndef STATEMENTS_H
#define STATEMENTS_H

#include <vector>
#include <string>

// Forward declarations
struct Expr;
struct Token;
enum class TokenType;
class Parser;

struct SelectStmt {
    std::vector<Expr*> columns;
    std::string table;
    Expr* where = nullptr;
    std::vector<Expr*> order_by;  // Optional
    std::vector<Expr*> group_by;  // Optional
};

// Function to parse SELECT statement
SelectStmt parse_select(Parser& parser);

#endif // STATEMENTS_H
