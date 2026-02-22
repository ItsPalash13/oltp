#include "statements.cpp"

int main() {
    std::string sql =
        "SELECT price * discount / 100 "
        "FROM products "
        "WHERE price >= 100 AND discount < 20;";

    Parser parser(sql);
    SelectStmt stmt = parse_statement(parser);

    std::cout << "Parsed SELECT on table: " << stmt.table << "\n";
    
    // Example with ORDER BY and GROUP BY
    std::string sql2 =
        "SELECT name, price "
        "FROM products "
        "WHERE price >= 100 "
        "ORDER BY price "
        "GROUP BY category;";
    
    Parser parser2(sql2);
    SelectStmt stmt2 = parse_statement(parser2);
    
    std::cout << "Parsed SELECT on table: " << stmt2.table << "\n";
    std::cout << "ORDER BY columns: " << stmt2.order_by.size() << "\n";
    std::cout << "GROUP BY columns: " << stmt2.group_by.size() << "\n";
    
    return 0;
}
