#include "planner/planner.h"
#include "../parser/statements.cpp"
#include <iostream>

// Helper function to print plan type
const char* plan_type_name(PlanType type) {
    switch (type) {
        case PlanType::SeqScan: return "SeqScan";
        case PlanType::IndexScan: return "IndexScan";
        case PlanType::Filter: return "Filter";
        case PlanType::Project: return "Project";
        case PlanType::Sort: return "Sort";
        case PlanType::Insert: return "Insert";
        case PlanType::Update: return "Update";
        case PlanType::Delete: return "Delete";
        case PlanType::Collect: return "Collect";
        case PlanType::Values: return "Values";
        default: return "Unknown";
    }
}

// Helper function to print plan tree structure
void print_plan_tree(const Plan* plan, int indent = 0) {
    if (!plan) return;
    
    for (int i = 0; i < indent; i++) {
        std::cout << "  ";
    }
    std::cout << "- " << plan_type_name(plan->type);
    
    // Print plan-specific details
    switch (plan->type) {
        case PlanType::SeqScan: {
            const SeqScanPlan* scan = static_cast<const SeqScanPlan*>(plan);
            std::cout << " (table: " << scan->table << ")";
            break;
        }
        case PlanType::Filter: {
            std::cout << " (WHERE clause)";
            const FilterPlan* filter = static_cast<const FilterPlan*>(plan);
            if (filter->source) {
                std::cout << "\n";
                print_plan_tree(filter->source.get(), indent + 1);
            }
            return; // Already printed source
        }
        case PlanType::Project: {
            const ProjectPlan* project = static_cast<const ProjectPlan*>(plan);
            std::cout << " (" << project->projections.size() << " columns)";
            if (project->source) {
                std::cout << "\n";
                print_plan_tree(project->source.get(), indent + 1);
            }
            return; // Already printed source
        }
        case PlanType::Sort: {
            const SortPlan* sort = static_cast<const SortPlan*>(plan);
            std::cout << " (" << sort->order_by.size() << " order by expressions)";
            if (sort->source) {
                std::cout << "\n";
                print_plan_tree(sort->source.get(), indent + 1);
            }
            return; // Already printed source
        }
        case PlanType::Insert: {
            const InsertPlan* insert = static_cast<const InsertPlan*>(plan);
            std::cout << " (table: " << insert->table << ", " << insert->columns.size() << " columns)";
            if (insert->source) {
                std::cout << "\n";
                print_plan_tree(insert->source.get(), indent + 1);
            }
            return; // Already printed source
        }
        case PlanType::Update: {
            const UpdatePlan* update = static_cast<const UpdatePlan*>(plan);
            std::cout << " (table: " << update->table << ", " << update->assignments.size() << " assignments)";
            if (update->source) {
                std::cout << "\n";
                print_plan_tree(update->source.get(), indent + 1);
            }
            return; // Already printed source
        }
        case PlanType::Delete: {
            const DeletePlan* del = static_cast<const DeletePlan*>(plan);
            std::cout << " (table: " << del->table << ")";
            if (del->source) {
                std::cout << "\n";
                print_plan_tree(del->source.get(), indent + 1);
            }
            return; // Already printed source
        }
        case PlanType::Collect: {
            const CollectPlan* collect = static_cast<const CollectPlan*>(plan);
            std::cout << " (materialization barrier)";
            if (collect->source) {
                std::cout << "\n";
                print_plan_tree(collect->source.get(), indent + 1);
            }
            return; // Already printed source
        }
        case PlanType::Values: {
            const ValuesPlan* values = static_cast<const ValuesPlan*>(plan);
            std::cout << " (" << values->values.size() << " values)";
            break;
        }
        default:
            break;
    }
    std::cout << "\n";
}

int main() {
    std::cout << "=== Query Planner Examples ===\n\n";
    
    try {
        // Example 1: Simple SELECT
        std::cout << "--- Example 1: Simple SELECT ---\n";
        std::string sql1 = "SELECT id, name FROM users;";
        std::cout << "SQL: " << sql1 << "\n";
        
        Parser parser1(sql1);
        Statement stmt1 = parse_statement(parser1);
        std::unique_ptr<Plan> plan1 = build_plan(stmt1);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan1.get());
        std::cout << "\n";
        
        // Example 2: SELECT with WHERE
        std::cout << "--- Example 2: SELECT with WHERE ---\n";
        std::string sql2 = "SELECT price, discount FROM products WHERE price >= 100;";
        std::cout << "SQL: " << sql2 << "\n";
        
        Parser parser2(sql2);
        Statement stmt2 = parse_statement(parser2);
        std::unique_ptr<Plan> plan2 = build_plan(stmt2);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan2.get());
        std::cout << "\n";
        
        // Example 3: SELECT with WHERE and ORDER BY
        std::cout << "--- Example 3: SELECT with WHERE and ORDER BY ---\n";
        std::string sql3 = 
            "SELECT price * discount / 100 "
            "FROM products "
            "WHERE price >= 100 AND discount < 20 "
            "ORDER BY price;";
        std::cout << "SQL: " << sql3 << "\n";
        
        Parser parser3(sql3);
        Statement stmt3 = parse_statement(parser3);
        std::unique_ptr<Plan> plan3 = build_plan(stmt3);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan3.get());
        std::cout << "\n";
        
        // Example 4: INSERT with column list
        std::cout << "--- Example 4: INSERT with column list ---\n";
        std::string sql4 = "INSERT INTO users (id, name, email) VALUES (1, 'John', 'john@example.com');";
        std::cout << "SQL: " << sql4 << "\n";
        
        Parser parser4(sql4);
        Statement stmt4 = parse_statement(parser4);
        std::unique_ptr<Plan> plan4 = build_plan(stmt4);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan4.get());
        std::cout << "\n";
        
        // Example 5: INSERT without column list
        std::cout << "--- Example 5: INSERT without column list ---\n";
        std::string sql5 = "INSERT INTO products VALUES (100, 'Product Name', 50);";
        std::cout << "SQL: " << sql5 << "\n";
        
        Parser parser5(sql5);
        Statement stmt5 = parse_statement(parser5);
        std::unique_ptr<Plan> plan5 = build_plan(stmt5);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan5.get());
        std::cout << "\n";
        
        // Example 6: UPDATE with WHERE
        std::cout << "--- Example 6: UPDATE with WHERE ---\n";
        std::string sql6 = "UPDATE users SET name = 'John', email = 'john@example.com' WHERE id = 1;";
        std::cout << "SQL: " << sql6 << "\n";
        
        Parser parser6(sql6);
        Statement stmt6 = parse_statement(parser6);
        std::unique_ptr<Plan> plan6 = build_plan(stmt6);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan6.get());
        std::cout << "\n";
        
        // Example 7: UPDATE with expression
        std::cout << "--- Example 7: UPDATE with expression ---\n";
        std::string sql7 = "UPDATE products SET price = price * 0.9 WHERE price > 100;";
        std::cout << "SQL: " << sql7 << "\n";
        
        Parser parser7(sql7);
        Statement stmt7 = parse_statement(parser7);
        std::unique_ptr<Plan> plan7 = build_plan(stmt7);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan7.get());
        std::cout << "\n";
        
        // Example 8: DELETE with WHERE
        std::cout << "--- Example 8: DELETE with WHERE ---\n";
        std::string sql8 = "DELETE FROM users WHERE id = 1;";
        std::cout << "SQL: " << sql8 << "\n";
        
        Parser parser8(sql8);
        Statement stmt8 = parse_statement(parser8);
        std::unique_ptr<Plan> plan8 = build_plan(stmt8);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan8.get());
        std::cout << "\n";
        
        // Example 9: DELETE without WHERE
        std::cout << "--- Example 9: DELETE without WHERE ---\n";
        std::string sql9 = "DELETE FROM products;";
        std::cout << "SQL: " << sql9 << "\n";
        
        Parser parser9(sql9);
        Statement stmt9 = parse_statement(parser9);
        std::unique_ptr<Plan> plan9 = build_plan(stmt9);
        
        std::cout << "Plan tree:\n";
        print_plan_tree(plan9.get());
        std::cout << "\n";
        
        std::cout << "=== All examples completed successfully! ===\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
