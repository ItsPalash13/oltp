#define PARSER_SKIP_MAIN
#include "parser.cpp"
#undef PARSER_SKIP_MAIN

#include "parser/statements/select.h"
#include "parser/statements/create.h"
#include "parser/statements/insert.h"
#include "parser/statements/update.h"
#include "parser/statements/delete.h"
#include "parser/statements/use.h"
#include "parser/statements/describe.h"
#include "parser/statements/catalog.h"

// Parse CATALOG LIST; | CATALOG READ table; | CATALOG VIEW; | CATALOG EVICT table;
CatalogStmt parse_catalog(Parser& parser) {
    CatalogStmt stmt;
    stmt.table_name = "";
    parser.eat(TokenType::Catalog);
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected LIST, READ, VIEW, or EVICT after CATALOG");
    }
    std::string op = parser.current.text;
    parser.eat(TokenType::Identifier);
    if (op == "LIST") {
        stmt.op = CatalogOp::List;
    } else if (op == "READ") {
        stmt.op = CatalogOp::Read;
        if (parser.current.type != TokenType::Identifier) {
            throw std::runtime_error("Expected table name after CATALOG READ");
        }
        stmt.table_name = parser.current.text;
        parser.eat(TokenType::Identifier);
    } else if (op == "VIEW") {
        stmt.op = CatalogOp::View;
    } else if (op == "EVICT") {
        stmt.op = CatalogOp::Evict;
        if (parser.current.type != TokenType::Identifier) {
            throw std::runtime_error("Expected table name after CATALOG EVICT");
        }
        stmt.table_name = parser.current.text;
        parser.eat(TokenType::Identifier);
    } else {
        throw std::runtime_error("Expected LIST, READ, VIEW, or EVICT after CATALOG");
    }
    parser.eat(TokenType::Semicolon);
    return stmt;
}

// Parse DESCRIBE table_name; (optional: DESCRIBE TABLE table_name;)
DescribeStmt parse_describe(Parser& parser) {
    DescribeStmt stmt;
    parser.eat(TokenType::Describe);
    if (parser.current.type == TokenType::Table) {
        parser.eat(TokenType::Table);
    }
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected table name after DESCRIBE");
    }
    stmt.table_name = parser.current.text;
    parser.eat(TokenType::Identifier);
    parser.eat(TokenType::Semicolon);
    return stmt;
}

// Parse USE database_name;
UseStmt parse_use(Parser& parser) {
    UseStmt stmt;
    parser.eat(TokenType::Use);
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected database name after USE");
    }
    stmt.database_name = parser.current.text;
    parser.eat(TokenType::Identifier);
    parser.eat(TokenType::Semicolon);
    return stmt;
}

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

// Parse a single column definition
// Format: name datatype [PRIMARY KEY] [UNIQUE] [NOT NULL]
ColumnDef parse_column_def(Parser& parser) {
    ColumnDef col;
    
    // Parse column name
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected column name");
    }
    col.name = parser.current.text;
    parser.eat(TokenType::Identifier);
    
    // Parse data type (may include parentheses like VARCHAR(255) or DECIMAL(10,2))
    std::string data_type = "";
    if (parser.current.type == TokenType::Identifier) {
        data_type = parser.current.text;
        parser.eat(TokenType::Identifier);
        
        // Check for parameterized types like VARCHAR(255) or DECIMAL(10,2)
        if (parser.current.type == TokenType::LParen) {
            parser.eat(TokenType::LParen);
            data_type += "(";
            
            // Parse first parameter (required)
            if (parser.current.type == TokenType::Number) {
                data_type += parser.current.text;
                parser.eat(TokenType::Number);
            } else {
                throw std::runtime_error("Expected number in data type parameter");
            }
            
            // Parse additional comma-separated parameters (e.g., DECIMAL(10,2))
            while (parser.current.type == TokenType::Comma) {
                parser.eat(TokenType::Comma);
                data_type += ",";
                if (parser.current.type == TokenType::Number) {
                    data_type += parser.current.text;
                    parser.eat(TokenType::Number);
                } else {
                    throw std::runtime_error("Expected number after comma in data type parameter");
                }
            }
            
            // Close parentheses
            if (parser.current.type == TokenType::RParen) {
                data_type += ")";
                parser.eat(TokenType::RParen);
            } else {
                throw std::runtime_error("Expected closing parenthesis in data type");
            }
        }
    } else {
        throw std::runtime_error("Expected data type");
    }
    col.data_type = data_type;
    
    // Parse optional constraints (can appear in any order)
    while (parser.current.type == TokenType::Primary || 
           parser.current.type == TokenType::Unique || 
           parser.current.type == TokenType::Not ||
           parser.current.type == TokenType::AutoIncrement) {
        
        if (parser.current.type == TokenType::Primary) {
            parser.eat(TokenType::Primary);
            parser.eat(TokenType::Key);
            col.is_primary_key = true;
        } else if (parser.current.type == TokenType::Unique) {
            parser.eat(TokenType::Unique);
            col.is_unique = true;
        } else if (parser.current.type == TokenType::Not) {
            parser.eat(TokenType::Not);
            parser.eat(TokenType::Null);
            col.is_not_null = true;
        } else if (parser.current.type == TokenType::AutoIncrement) {
            parser.eat(TokenType::AutoIncrement);
            col.is_auto_increment = true;
        }
    }
    
    return col;
}

// Parse CREATE DATABASE statement (CREATE token already consumed)
// Format: DATABASE database_name;
CreateDatabaseStmt parse_create_database(Parser& parser) {
    CreateDatabaseStmt stmt;
    
    parser.eat(TokenType::Database);
    
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected database name");
    }
    stmt.database_name = parser.current.text;
    parser.eat(TokenType::Identifier);
    
    parser.eat(TokenType::Semicolon);
    return stmt;
}

// Parse CREATE TABLE statement (CREATE token already consumed)
// Format: TABLE table_name IN database (column_definitions);
CreateTableStmt parse_create_table(Parser& parser) {
    CreateTableStmt stmt;
    
    parser.eat(TokenType::Table);
    
    // Parse table name
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected table name");
    }
    stmt.table_name = parser.current.text;
    parser.eat(TokenType::Identifier);
    
    // Parse column definitions in parentheses
    parser.eat(TokenType::LParen);
    
    // Parse first column
    stmt.columns.push_back(parse_column_def(parser));
    
    // Parse remaining columns (comma-separated)
    while (parser.current.type == TokenType::Comma) {
        parser.eat(TokenType::Comma);
        stmt.columns.push_back(parse_column_def(parser));
    }
    
    parser.eat(TokenType::RParen);
    parser.eat(TokenType::Semicolon);
    return stmt;
}

// Main CREATE parser - routes to DATABASE or TABLE parser
CreateStmt parse_create(Parser& parser) {
    if (parser.current.type != TokenType::Create) {
        throw std::runtime_error("Expected CREATE keyword");
    }
    
    // Eat CREATE and check next token
    parser.eat(TokenType::Create);
    
    if (parser.current.type == TokenType::Database) {
        CreateDatabaseStmt db_stmt = parse_create_database(parser);
        return CreateStmt(db_stmt);
    } else if (parser.current.type == TokenType::Table) {
        CreateTableStmt table_stmt = parse_create_table(parser);
        return CreateStmt(table_stmt);
    } else {
        throw std::runtime_error("Expected DATABASE or TABLE after CREATE");
    }
}

// Parse INSERT statement
// Format: INSERT INTO table (columns) VALUES (values); or INSERT INTO table VALUES (values);
InsertStmt parse_insert(Parser& parser) {
    InsertStmt stmt;
    
    parser.eat(TokenType::Insert);
    parser.eat(TokenType::Into);
    
    // Parse table name
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected table name");
    }
    stmt.table = parser.current.text;
    parser.eat(TokenType::Identifier);
    
    // Parse optional column list
    if (parser.current.type == TokenType::LParen) {
        parser.eat(TokenType::LParen);
        
        // Parse first column name
        if (parser.current.type != TokenType::Identifier) {
            throw std::runtime_error("Expected column name");
        }
        stmt.columns.push_back(parser.current.text);
        parser.eat(TokenType::Identifier);
        
        // Parse remaining column names (comma-separated)
        while (parser.current.type == TokenType::Comma) {
            parser.eat(TokenType::Comma);
            if (parser.current.type != TokenType::Identifier) {
                throw std::runtime_error("Expected column name");
            }
            stmt.columns.push_back(parser.current.text);
            parser.eat(TokenType::Identifier);
        }
        
        parser.eat(TokenType::RParen);
    }
    
    // Parse VALUES keyword
    parser.eat(TokenType::Values);
    
    // Parse values in parentheses
    parser.eat(TokenType::LParen);
    
    // Parse first value
    stmt.values.push_back(parser.parse_expr());
    
    // Parse remaining values (comma-separated)
    while (parser.current.type == TokenType::Comma) {
        parser.eat(TokenType::Comma);
        stmt.values.push_back(parser.parse_expr());
    }
    
    parser.eat(TokenType::RParen);
    parser.eat(TokenType::Semicolon);
    return stmt;
}

// Parse assignment (column = value) for UPDATE
Assignment parse_assignment(Parser& parser) {
    Assignment assign;
    
    // Parse column name
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected column name in assignment");
    }
    assign.column = parser.current.text;
    parser.eat(TokenType::Identifier);
    
    // Parse = sign
    parser.eat(TokenType::Eq);
    
    // Parse value expression
    assign.value = parser.parse_expr();
    
    return assign;
}

// Parse UPDATE statement
// Format: UPDATE table SET col1 = val1, col2 = val2 WHERE condition;
UpdateStmt parse_update(Parser& parser) {
    UpdateStmt stmt;
    
    parser.eat(TokenType::Update);
    
    // Parse table name
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected table name");
    }
    stmt.table = parser.current.text;
    parser.eat(TokenType::Identifier);
    
    // Parse SET keyword
    parser.eat(TokenType::Set);
    
    // Parse first assignment
    stmt.assignments.push_back(parse_assignment(parser));
    
    // Parse remaining assignments (comma-separated)
    while (parser.current.type == TokenType::Comma) {
        parser.eat(TokenType::Comma);
        stmt.assignments.push_back(parse_assignment(parser));
    }
    
    // Parse optional WHERE clause
    if (parser.current.type == TokenType::Where) {
        parser.eat(TokenType::Where);
        stmt.where = parser.parse_expr();
    }
    
    parser.eat(TokenType::Semicolon);
    return stmt;
}

// Parse DELETE statement
// Format: DELETE FROM table WHERE condition; or DELETE FROM table;
DeleteStmt parse_delete(Parser& parser) {
    DeleteStmt stmt;
    
    parser.eat(TokenType::Delete);
    parser.eat(TokenType::From);
    
    // Parse table name
    if (parser.current.type != TokenType::Identifier) {
        throw std::runtime_error("Expected table name");
    }
    stmt.table = parser.current.text;
    parser.eat(TokenType::Identifier);
    
    // Parse optional WHERE clause
    if (parser.current.type == TokenType::Where) {
        parser.eat(TokenType::Where);
        stmt.where = parser.parse_expr();
    }
    
    parser.eat(TokenType::Semicolon);
    return stmt;
}
