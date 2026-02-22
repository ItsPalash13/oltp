#include "executor/executor.h"
#include "executor/storage.h"
#include "planner/plan.h"
#include "executor/expr_defs.h"
#include <iostream>
#include <map>
#include <vector>
#include <string>

// Helper to print a tuple
void print_tuple(const Tuple& tuple) {
    std::cout << "[";
    for (size_t i = 0; i < tuple.size(); i++) {
        if (i > 0) std::cout << ", ";
        if (std::holds_alternative<int>(tuple[i])) {
            std::cout << std::get<int>(tuple[i]);
        } else if (std::holds_alternative<std::string>(tuple[i])) {
            std::cout << "\"" << std::get<std::string>(tuple[i]) << "\"";
        }
    }
    std::cout << "]";
}

// Helper to print all tuples
void print_results(const std::vector<Tuple>& results) {
    std::cout << "Results (" << results.size() << " rows):\n";
    for (const auto& tuple : results) {
        print_tuple(tuple);
        std::cout << "\n";
    }
}

int main() {
    std::cout << "=== Execution Engine Test ===\n\n";
    
    // Initialize storage with dummy data
    Storage storage;
    
    // Define schema: table name -> column names
    std::map<std::string, std::vector<std::string>> schema;
    schema["users"] = {"id", "name", "age"};
    
    try {
        // Test 1: Simple SeqScan
        std::cout << "--- Test 1: Simple SeqScan ---\n";
        auto scan_plan = std::make_unique<SeqScanPlan>("users");
        std::vector<Tuple> results1 = execute_plan(scan_plan.get(), storage, schema);
        print_results(results1);
        std::cout << "\n";
        
        // Test 2: Filter (WHERE age >= 18)
        std::cout << "--- Test 2: Filter (WHERE age >= 18) ---\n";
        auto scan_plan2 = std::make_unique<SeqScanPlan>("users");
        auto age_expr = new IdentifierExpr("age");
        auto age_18 = new NumberExpr(18);
        auto predicate = new BinaryExpr(">=", age_expr, age_18);
        auto filter_plan = std::make_unique<FilterPlan>(predicate, std::move(scan_plan2));
        std::vector<Tuple> results2 = execute_plan(filter_plan.get(), storage, schema);
        print_results(results2);
        std::cout << "\n";
        
        // Test 3: Project (SELECT id, name)
        std::cout << "--- Test 3: Project (SELECT id, name) ---\n";
        auto scan_plan3 = std::make_unique<SeqScanPlan>("users");
        auto id_expr = new IdentifierExpr("id");
        auto name_expr = new IdentifierExpr("name");
        std::vector<Expr*> projections = {id_expr, name_expr};
        auto project_plan = std::make_unique<ProjectPlan>(projections, std::move(scan_plan3));
        std::vector<Tuple> results3 = execute_plan(project_plan.get(), storage, schema);
        print_results(results3);
        std::cout << "\n";
        
        // Test 4: Filter + Project (SELECT id, name WHERE age >= 18)
        std::cout << "--- Test 4: Filter + Project (SELECT id, name WHERE age >= 18) ---\n";
        auto scan_plan4 = std::make_unique<SeqScanPlan>("users");
        auto age_expr2 = new IdentifierExpr("age");
        auto age_18_2 = new NumberExpr(18);
        auto predicate2 = new BinaryExpr(">=", age_expr2, age_18_2);
        auto filter_plan2 = std::make_unique<FilterPlan>(predicate2, std::move(scan_plan4));
        auto id_expr2 = new IdentifierExpr("id");
        auto name_expr2 = new IdentifierExpr("name");
        std::vector<Expr*> projections2 = {id_expr2, name_expr2};
        auto project_plan2 = std::make_unique<ProjectPlan>(projections2, std::move(filter_plan2));
        std::vector<Tuple> results4 = execute_plan(project_plan2.get(), storage, schema);
        print_results(results4);
        std::cout << "\n";
        
        // Test 5: Project with expression (SELECT id, age * 2)
        std::cout << "--- Test 5: Project with expression (SELECT id, age * 2) ---\n";
        auto scan_plan5 = std::make_unique<SeqScanPlan>("users");
        auto id_expr3 = new IdentifierExpr("id");
        auto age_expr3 = new IdentifierExpr("age");
        auto two = new NumberExpr(2);
        auto age_times_2 = new BinaryExpr("*", age_expr3, two);
        std::vector<Expr*> projections3 = {id_expr3, age_times_2};
        auto project_plan3 = std::make_unique<ProjectPlan>(projections3, std::move(scan_plan5));
        std::vector<Tuple> results5 = execute_plan(project_plan3.get(), storage, schema);
        print_results(results5);
        std::cout << "\n";
        
        std::cout << "=== All tests completed successfully! ===\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
