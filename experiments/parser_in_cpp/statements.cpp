#define PARSER_SKIP_MAIN
#include "parser.cpp"
#undef PARSER_SKIP_MAIN

SelectStmt parse_select(Parser& parser) {
    SelectStmt stmt;

    parser.eat(TokenType::Select);

    // Parse columns
    stmt.columns.push_back(parser.parse_expr());
    while (parser.current.type == TokenType::Comma) {
        parser.eat(TokenType::Comma);
        stmt.columns.push_back(parser.parse_expr());
    }

    parser.eat(TokenType::From);
    stmt.table = parser.current.text;
    parser.eat(TokenType::Identifier);

    // Optional WHERE clause
    if (parser.current.type == TokenType::Where) {
        parser.eat(TokenType::Where);
        stmt.where = parser.parse_expr();
    }

    // Optional ORDER BY and GROUP BY clauses (can appear in any order)
    while (parser.current.type == TokenType::OrderBy || parser.current.type == TokenType::GroupBy) {
        if (parser.current.type == TokenType::OrderBy) {
            parser.eat(TokenType::OrderBy);
            parser.eat(TokenType::By);
            
            // Parse ORDER BY expressions
            stmt.order_by.push_back(parser.parse_expr());
            while (parser.current.type == TokenType::Comma) {
                parser.eat(TokenType::Comma);
                stmt.order_by.push_back(parser.parse_expr());
            }
        } else if (parser.current.type == TokenType::GroupBy) {
            parser.eat(TokenType::GroupBy);
            parser.eat(TokenType::By);
            
            // Parse GROUP BY expressions
            stmt.group_by.push_back(parser.parse_expr());
            while (parser.current.type == TokenType::Comma) {
                parser.eat(TokenType::Comma);
                stmt.group_by.push_back(parser.parse_expr());
            }
        }
    }

    parser.eat(TokenType::Semicolon);
    return stmt;
}
