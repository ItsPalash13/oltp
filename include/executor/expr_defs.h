#ifndef EXPR_DEFS_H
#define EXPR_DEFS_H

#include <string>

// Forward declarations from parser
struct Token;
enum class TokenType;

// Expression AST definitions (from parser.cpp)
enum class ExprKind { Identifier, Number, String, Unary, Binary, Star };

struct Expr {
    ExprKind kind;
};

// SELECT * : all columns (expanded to column list at plan/executor level)
struct StarExpr : Expr {
    StarExpr();
};

struct IdentifierExpr : Expr {
    std::string name;
    IdentifierExpr(const std::string& n);
};

struct NumberExpr : Expr {
    int value;
    NumberExpr(int v);
};

struct StringExpr : Expr {
    std::string value;
    StringExpr(const std::string& v);
};

struct BinaryExpr : Expr {
    std::string op;
    Expr* left;
    Expr* right;

    BinaryExpr(const std::string& o, Expr* l, Expr* r);
};

#endif // EXPR_DEFS_H
