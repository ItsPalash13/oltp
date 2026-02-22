#include "executor/evaluator.h"
#include "executor/expr_defs.h"
#include <stdexcept>
#include <variant>
#include <iostream>

// Helper to get integer from Value
static int get_int(const Value& v) {
    if (std::holds_alternative<int>(v)) {
        return std::get<int>(v);
    }
    throw std::runtime_error("Expected integer value");
}

// Helper to get string from Value
static std::string get_string(const Value& v) {
    if (std::holds_alternative<std::string>(v)) {
        return std::get<std::string>(v);
    }
    if (std::holds_alternative<int>(v)) {
        return std::to_string(std::get<int>(v));
    }
    throw std::runtime_error("Expected string value");
}

// Find column index by name
static int find_column_index(const std::string& name, 
                             const std::vector<std::string>& column_names) {
    for (size_t i = 0; i < column_names.size(); i++) {
        if (column_names[i] == name) {
            return static_cast<int>(i);
        }
    }
    throw std::runtime_error("Column not found: " + name);
}

Value evaluate_expr(Expr* expr, const Tuple& tuple, 
                    const std::vector<std::string>& column_names) {
    if (!expr) {
        throw std::runtime_error("Null expression");
    }

    switch (expr->kind) {
        case ExprKind::Star:
            throw std::runtime_error("Star must be expanded before evaluation");
        case ExprKind::Identifier: {
            IdentifierExpr* ident = static_cast<IdentifierExpr*>(expr);
            int idx = find_column_index(ident->name, column_names);
            if (idx < 0 || static_cast<size_t>(idx) >= tuple.size()) {
                throw std::runtime_error("Column index out of bounds: " + ident->name);
            }
            return tuple[idx];
        }
        
        case ExprKind::Number: {
            NumberExpr* num = static_cast<NumberExpr*>(expr);
            return num->value;
        }
        
        case ExprKind::String: {
            StringExpr* str = static_cast<StringExpr*>(expr);
            return str->value;
        }
        
        case ExprKind::Binary: {
            BinaryExpr* bin = static_cast<BinaryExpr*>(expr);
            Value left_val = evaluate_expr(bin->left, tuple, column_names);
            Value right_val = evaluate_expr(bin->right, tuple, column_names);
            
            // Handle arithmetic operators
            if (bin->op == "+") {
                if (std::holds_alternative<int>(left_val) && std::holds_alternative<int>(right_val)) {
                    return std::get<int>(left_val) + std::get<int>(right_val);
                }
                // String concatenation
                return get_string(left_val) + get_string(right_val);
            }
            if (bin->op == "-") {
                return get_int(left_val) - get_int(right_val);
            }
            if (bin->op == "*") {
                return get_int(left_val) * get_int(right_val);
            }
            if (bin->op == "/") {
                int right = get_int(right_val);
                if (right == 0) throw std::runtime_error("Division by zero");
                return get_int(left_val) / right;
            }
            
            // Comparison operators return boolean (as int: 1 for true, 0 for false)
            if (bin->op == "=" || bin->op == "==") {
                if (std::holds_alternative<int>(left_val) && std::holds_alternative<int>(right_val)) {
                    return (std::get<int>(left_val) == std::get<int>(right_val)) ? 1 : 0;
                }
                if (std::holds_alternative<std::string>(left_val) && std::holds_alternative<std::string>(right_val)) {
                    return (std::get<std::string>(left_val) == std::get<std::string>(right_val)) ? 1 : 0;
                }
                return 0;
            }
            if (bin->op == "<") {
                if (std::holds_alternative<int>(left_val) && std::holds_alternative<int>(right_val)) {
                    return (std::get<int>(left_val) < std::get<int>(right_val)) ? 1 : 0;
                }
                if (std::holds_alternative<std::string>(left_val) && std::holds_alternative<std::string>(right_val)) {
                    return (std::get<std::string>(left_val) < std::get<std::string>(right_val)) ? 1 : 0;
                }
                return 0;
            }
            if (bin->op == ">") {
                if (std::holds_alternative<int>(left_val) && std::holds_alternative<int>(right_val)) {
                    return (std::get<int>(left_val) > std::get<int>(right_val)) ? 1 : 0;
                }
                if (std::holds_alternative<std::string>(left_val) && std::holds_alternative<std::string>(right_val)) {
                    return (std::get<std::string>(left_val) > std::get<std::string>(right_val)) ? 1 : 0;
                }
                return 0;
            }
            if (bin->op == "<=") {
                if (std::holds_alternative<int>(left_val) && std::holds_alternative<int>(right_val)) {
                    return (std::get<int>(left_val) <= std::get<int>(right_val)) ? 1 : 0;
                }
                if (std::holds_alternative<std::string>(left_val) && std::holds_alternative<std::string>(right_val)) {
                    return (std::get<std::string>(left_val) <= std::get<std::string>(right_val)) ? 1 : 0;
                }
                return 0;
            }
            if (bin->op == ">=") {
                if (std::holds_alternative<int>(left_val) && std::holds_alternative<int>(right_val)) {
                    return (std::get<int>(left_val) >= std::get<int>(right_val)) ? 1 : 0;
                }
                if (std::holds_alternative<std::string>(left_val) && std::holds_alternative<std::string>(right_val)) {
                    return (std::get<std::string>(left_val) >= std::get<std::string>(right_val)) ? 1 : 0;
                }
                return 0;
            }
            if (bin->op == "AND" || bin->op == "&&") {
                int left = get_int(left_val);
                int right = get_int(right_val);
                return (left != 0 && right != 0) ? 1 : 0;
            }
            if (bin->op == "OR" || bin->op == "||") {
                int left = get_int(left_val);
                int right = get_int(right_val);
                return (left != 0 || right != 0) ? 1 : 0;
            }
            
            throw std::runtime_error("Unknown binary operator: " + bin->op);
        }
        
        default:
            throw std::runtime_error("Unsupported expression kind");
    }
}

bool evaluate_predicate(Expr* predicate, const Tuple& tuple,
                       const std::vector<std::string>& column_names) {
    Value result = evaluate_expr(predicate, tuple, column_names);
    // Treat non-zero as true, zero as false
    if (std::holds_alternative<int>(result)) {
        return std::get<int>(result) != 0;
    }
    // String values are considered true
    return true;
}
