#include "analyser/expr_utils.h"
#include "executor/expr_defs.h"
#include "parser/statements/create.h"
#include <stdexcept>

void collect_column_refs(Expr* expr, std::vector<std::string>& out) {
    if (!expr) return;
    switch (expr->kind) {
        case ExprKind::Identifier:
            out.push_back(static_cast<IdentifierExpr*>(expr)->name);
            break;
        case ExprKind::Binary: {
            BinaryExpr* b = static_cast<BinaryExpr*>(expr);
            collect_column_refs(b->left, out);
            collect_column_refs(b->right, out);
            break;
        }
        case ExprKind::Number:
        case ExprKind::String:
        case ExprKind::Unary:
        default:
            break;
    }
}

void validate_columns_exist(const std::vector<std::string>& column_names,
                            const CreateTableStmt& schema,
                            const std::string& table_name) {
    for (const std::string& name : column_names) {
        bool found = false;
        for (const auto& col : schema.columns) {
            if (col.name == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("Column '" + name + "' not found in table '" + table_name + "'");
        }
    }
}
