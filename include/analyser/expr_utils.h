#ifndef ANALYSER_EXPR_UTILS_H
#define ANALYSER_EXPR_UTILS_H

#include <vector>
#include <string>

struct Expr;
struct CreateTableStmt;

// Collect all column references (IdentifierExpr names) from an expression tree.
void collect_column_refs(Expr* expr, std::vector<std::string>& out);

// Verify each column name exists in the schema; throw std::runtime_error if any missing.
void validate_columns_exist(const std::vector<std::string>& column_names,
                            const CreateTableStmt& schema,
                            const std::string& table_name);

#endif // ANALYSER_EXPR_UTILS_H
