#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>

// 1️⃣ Tokenizer (simple, brutal, sufficient)
enum class TokenType {
    Identifier,
    Number,

    Select, From, Where, And, Or,
    OrderBy, GroupBy, By,

    Plus, Minus, Star, Slash,
    Eq, Lt, Gt, LtEq, GtEq,

    Comma, Semicolon,
    LParen, RParen,

    End
};

struct Token {
    TokenType type;
    std::string text;
};

// Tokenizer (naive but honest)
class Lexer {
    std::string input;
    size_t pos = 0;

public:
    Lexer(const std::string& s) : input(s) {}

    Token next() {
        while (pos < input.size() && isspace(input[pos])) pos++;

        if (pos >= input.size())
            return {TokenType::End, ""};

        char c = input[pos];

        // Identifiers / keywords
        if (isalpha(c)) {
            size_t start = pos;
            while (pos < input.size() && isalnum(input[pos])) pos++;
            std::string word = input.substr(start, pos - start);

            if (word == "SELECT") return {TokenType::Select, word};
            if (word == "FROM")   return {TokenType::From, word};
            if (word == "WHERE")  return {TokenType::Where, word};
            if (word == "AND")    return {TokenType::And, word};
            if (word == "OR")     return {TokenType::Or, word};
            if (word == "ORDER")  return {TokenType::OrderBy, word};
            if (word == "GROUP")  return {TokenType::GroupBy, word};
            if (word == "BY")     return {TokenType::By, word};

            return {TokenType::Identifier, word};
        }

        // Numbers
        if (isdigit(c)) {
            size_t start = pos;
            while (pos < input.size() && isdigit(input[pos])) pos++;
            return {TokenType::Number, input.substr(start, pos - start)};
        }

        pos++;
        switch (c) {
            case '+': return {TokenType::Plus, "+"};
            case '-': return {TokenType::Minus, "-"};
            case '*': return {TokenType::Star, "*"};
            case '/': return {TokenType::Slash, "/"};
            case '=': return {TokenType::Eq, "="};
            case '<':
                if (pos < input.size() && input[pos] == '=') {
                    pos++;
                    return {TokenType::LtEq, "<="};
                }
                return {TokenType::Lt, "<"};
            case '>':
                if (pos < input.size() && input[pos] == '=') {
                    pos++;
                    return {TokenType::GtEq, ">="};
                }
                return {TokenType::Gt, ">"};
            case ',': return {TokenType::Comma, ","};
            case ';': return {TokenType::Semicolon, ";"};
            case '(': return {TokenType::LParen, "("};
            case ')': return {TokenType::RParen, ")"};
            default:
                throw std::runtime_error("Unknown character");
        }
    }
};

// 2️⃣ AST (chains of structs, like we discussed)
enum class ExprKind { Identifier, Number, Unary, Binary };

struct Expr {
    ExprKind kind;
};

struct IdentifierExpr : Expr {
    std::string name;
    IdentifierExpr(const std::string& n) : name(n) {
        kind = ExprKind::Identifier;
    }
};

struct NumberExpr : Expr {
    int value;
    NumberExpr(int v) : value(v) {
        kind = ExprKind::Number;
    }
};

struct BinaryExpr : Expr {
    std::string op;
    Expr* left;
    Expr* right;

    BinaryExpr(const std::string& o, Expr* l, Expr* r)
        : op(o), left(l), right(r) {
        kind = ExprKind::Binary;
    }
};

// 3️⃣ Pratt expression parser (this is the core)
// Precedence
int precedence(const Token& t) {
    switch (t.type) {
        case TokenType::Or:  return 5;
        case TokenType::And: return 10;
        case TokenType::Eq:
        case TokenType::Lt:
        case TokenType::Gt:
        case TokenType::LtEq:
        case TokenType::GtEq:  return 20;
        case TokenType::Plus:
        case TokenType::Minus: return 30;
        case TokenType::Star:
        case TokenType::Slash: return 40;
        default: return 0;
    }
}

// Parser skeleton
class Parser {
    Lexer lexer;
public:
    Token current;  // Made public for parse_select access
    Parser(const std::string& s) : lexer(s) {
        current = lexer.next();
    }

    void eat(TokenType t) {
        if (current.type != t)
            throw std::runtime_error("Unexpected token");
        current = lexer.next();
    }

    // Expression parsing
    Expr* parse_expr(int min_prec = 0) {
        Expr* left = parse_primary();

        while (precedence(current) > min_prec) {
            Token op = current;
            eat(current.type);

            Expr* right = parse_expr(precedence(op));
            left = new BinaryExpr(op.text, left, right);
        }
        return left;
    }

    // Primary expressions
    Expr* parse_primary() {
        if (current.type == TokenType::Identifier) {
            std::string name = current.text;
            eat(TokenType::Identifier);
            return new IdentifierExpr(name);
        }

        if (current.type == TokenType::Number) {
            int val = std::stoi(current.text);
            eat(TokenType::Number);
            return new NumberExpr(val);
        }

        if (current.type == TokenType::LParen) {
            eat(TokenType::LParen);
            Expr* e = parse_expr();
            eat(TokenType::RParen);
            return e;
        }

        throw std::runtime_error("Invalid expression");
    }

};

#include "statements.h"

// Main parse_statement function
SelectStmt parse_statement(Parser& parser) {
    if (parser.current.type == TokenType::Select) {
        return parse_select(parser);
    }
    throw std::runtime_error("Unsupported statement type");
}

// Main function moved to main.cpp

