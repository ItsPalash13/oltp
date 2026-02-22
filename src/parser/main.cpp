#include "statements.cpp"

int main() {
    std::string sql =
        "SELECT price * discount / 100 "
        "FROM products "
        "WHERE price >= 100 AND discount < 20;";

    Parser parser(sql);
    Statement stmt = parse_statement(parser);

    // Access SELECT statement through Statement abstraction
    if (stmt.get_type() == StatementType::Select) {
        const SelectStmt& select_stmt = stmt.as_select();
        std::cout << "Parsed SELECT on table: " << select_stmt.table << "\n";
    }
    
    // Example with ORDER BY and GROUP BY
    std::string sql2 =
        "SELECT name, price "
        "FROM products "
        "WHERE price >= 100 "
        "ORDER BY price "
        "GROUP BY category;";
    
    Parser parser2(sql2);
    Statement stmt2 = parse_statement(parser2);
    
    if (stmt2.get_type() == StatementType::Select) {
        const SelectStmt& select_stmt2 = stmt2.as_select();
        std::cout << "Parsed SELECT on table: " << select_stmt2.table << "\n";
        std::cout << "ORDER BY columns: " << select_stmt2.order_by.size() << "\n";
        std::cout << "GROUP BY columns: " << select_stmt2.group_by.size() << "\n";
    }
    
    std::cout << "\n--- CREATE DATABASE Example ---\n";
    std::string sql3 = "CREATE DATABASE mydb;";
    Parser parser3(sql3);
    Statement stmt3 = parse_statement(parser3);
    
    if (stmt3.get_type() == StatementType::Create) {
        const CreateStmt& create_stmt3 = stmt3.as_create();
        if (create_stmt3.is_database()) {
            const CreateDatabaseStmt& db_stmt = create_stmt3.as_database();
            std::cout << "Parsed CREATE DATABASE: " << db_stmt.database_name << "\n";
        }
    }
    
    std::cout << "\n--- CREATE TABLE Example ---\n";
    std::string sql4 =
        "CREATE TABLE users ("
        "id INT PRIMARY KEY, "
        "name VARCHAR(255) NOT NULL, "
        "email VARCHAR(255) UNIQUE, "
        "age INT"
        ");";
    Parser parser4(sql4);
    Statement stmt4 = parse_statement(parser4);
    
    if (stmt4.get_type() == StatementType::Create) {
        const CreateStmt& create_stmt4 = stmt4.as_create();
        if (create_stmt4.is_table()) {
            const CreateTableStmt& table_stmt = create_stmt4.as_table();
            std::cout << "Parsed CREATE TABLE: " << table_stmt.table_name << "\n";
            std::cout << "Columns (" << table_stmt.columns.size() << "):\n";
            for (const auto& col : table_stmt.columns) {
                std::cout << "  - " << col.name << " " << col.data_type;
                if (col.is_primary_key) std::cout << " PRIMARY KEY";
                if (col.is_unique) std::cout << " UNIQUE";
                if (col.is_not_null) std::cout << " NOT NULL";
                std::cout << "\n";
            }
        }
    }
    
    std::cout << "\n--- CREATE TABLE with Multiple Constraints Example ---\n";
    std::string sql5 =
        "CREATE TABLE products ("
        "id BIGINT PRIMARY KEY, "
        "name VARCHAR(100) NOT NULL UNIQUE, "
        "price DECIMAL(10,2) NOT NULL, "
        "description TEXT"
        ");";
    Parser parser5(sql5);
    Statement stmt5 = parse_statement(parser5);
    
    if (stmt5.get_type() == StatementType::Create) {
        const CreateStmt& create_stmt5 = stmt5.as_create();
        if (create_stmt5.is_table()) {
            const CreateTableStmt& table_stmt = create_stmt5.as_table();
            std::cout << "Parsed CREATE TABLE: " << table_stmt.table_name << "\n";
            std::cout << "Columns:\n";
            for (const auto& col : table_stmt.columns) {
                std::cout << "  - " << col.name << " " << col.data_type;
                if (col.is_primary_key) std::cout << " PRIMARY KEY";
                if (col.is_unique) std::cout << " UNIQUE";
                if (col.is_not_null) std::cout << " NOT NULL";
                std::cout << "\n";
            }
        }
    }
    
    std::cout << "\n--- INSERT Example ---\n";
    std::string sql6 = "INSERT INTO users (id, name, email) VALUES (1, name, email);";
    Parser parser6(sql6);
    Statement stmt6 = parse_statement(parser6);
    
    if (stmt6.get_type() == StatementType::Insert) {
        const InsertStmt& insert_stmt = stmt6.as_insert();
        std::cout << "Parsed INSERT INTO: " << insert_stmt.table << "\n";
        std::cout << "Columns (" << insert_stmt.columns.size() << "): ";
        for (const auto& col : insert_stmt.columns) {
            std::cout << col << " ";
        }
        std::cout << "\nValues (" << insert_stmt.values.size() << " expressions)\n";
    }
    
    std::cout << "\n--- INSERT without column list Example ---\n";
    std::string sql7 = "INSERT INTO products VALUES (100, product_name, 50);";
    Parser parser7(sql7);
    Statement stmt7 = parse_statement(parser7);
    
    if (stmt7.get_type() == StatementType::Insert) {
        const InsertStmt& insert_stmt = stmt7.as_insert();
        std::cout << "Parsed INSERT INTO: " << insert_stmt.table << "\n";
        std::cout << "No column list specified, values (" << insert_stmt.values.size() << " expressions)\n";
    }
    
    std::cout << "\n--- UPDATE Example ---\n";
    std::string sql8 = "UPDATE users SET name = 'John', email = 'john@example.com' WHERE id = 1;";
    Parser parser8(sql8);
    Statement stmt8 = parse_statement(parser8);
    
    if (stmt8.get_type() == StatementType::Update) {
        const UpdateStmt& update_stmt = stmt8.as_update();
        std::cout << "Parsed UPDATE: " << update_stmt.table << "\n";
        std::cout << "Assignments (" << update_stmt.assignments.size() << "):\n";
        for (const auto& assign : update_stmt.assignments) {
            std::cout << "  - " << assign.column << " = <expression>\n";
        }
        if (update_stmt.where) {
            std::cout << "WHERE clause: <expression>\n";
        }
    }
    
    std::cout << "\n--- UPDATE with expression Example ---\n";
    std::string sql9 = "UPDATE products SET price = price * 0.9 WHERE price > 100;";
    Parser parser9(sql9);
    Statement stmt9 = parse_statement(parser9);
    
    if (stmt9.get_type() == StatementType::Update) {
        const UpdateStmt& update_stmt = stmt9.as_update();
        std::cout << "Parsed UPDATE: " << update_stmt.table << "\n";
        std::cout << "Assignments:\n";
        for (const auto& assign : update_stmt.assignments) {
            std::cout << "  - " << assign.column << " = <expression>\n";
        }
        if (update_stmt.where) {
            std::cout << "WHERE clause: <expression>\n";
        }
    }
    
    std::cout << "\n--- DELETE Example ---\n";
    std::string sql10 = "DELETE FROM users WHERE id = 1;";
    Parser parser10(sql10);
    Statement stmt10 = parse_statement(parser10);
    
    if (stmt10.get_type() == StatementType::Delete) {
        const DeleteStmt& delete_stmt = stmt10.as_delete();
        std::cout << "Parsed DELETE FROM: " << delete_stmt.table << "\n";
        if (delete_stmt.where) {
            std::cout << "WHERE clause: <expression>\n";
        } else {
            std::cout << "No WHERE clause (deletes all rows)\n";
        }
    }
    
    std::cout << "\n--- DELETE without WHERE Example ---\n";
    std::string sql11 = "DELETE FROM products;";
    Parser parser11(sql11);
    Statement stmt11 = parse_statement(parser11);
    
    if (stmt11.get_type() == StatementType::Delete) {
        const DeleteStmt& delete_stmt = stmt11.as_delete();
        std::cout << "Parsed DELETE FROM: " << delete_stmt.table << "\n";
        std::cout << "No WHERE clause - will delete all rows\n";
    }
    
    return 0;
}
