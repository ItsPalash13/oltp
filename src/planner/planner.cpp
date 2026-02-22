#include "planner/planner.h"
#include "parser/statements/select.h"
#include "parser/statements/insert.h"
#include "parser/statements/update.h"
#include "parser/statements/delete.h"
#include "executor/expr_defs.h"
#include "parser/statements/create.h"
#include <memory>
#include <stdexcept>
#include <optional>
#include <utility>

// Helper: is expr a constant (Number or String)?
static bool is_constant(Expr* e) {
    if (!e) return false;
    return e->kind == ExprKind::Number || e->kind == ExprKind::String;
}

// Helper: is expr an Identifier with the given name?
static bool is_identifier(Expr* e, const std::string& name) {
    if (!e || e->kind != ExprKind::Identifier) return false;
    return static_cast<IdentifierExpr*>(e)->name == name;
}

// Extract (start_key_expr, end_key_expr) when WHERE is a range on pk_col.
// Recognizes: pk = const -> (const, const); pk >= const -> (const, nullptr);
// pk > const -> (const, nullptr) with need_strict_filter; pk >= A AND pk <= B -> (A, B).
// Returns nullopt if WHERE is not a range on pk_col. need_strict_filter is set when we use pk > const.
static std::optional<std::pair<Expr*, Expr*>> extract_pk_range(Expr* where_expr, const std::string& pk_col,
                                                                bool* need_strict_filter = nullptr) {
    if (need_strict_filter) *need_strict_filter = false;
    if (!where_expr || where_expr->kind != ExprKind::Binary) return std::nullopt;
    BinaryExpr* bin = static_cast<BinaryExpr*>(where_expr);
    if (bin->op == "=") {
        if (is_identifier(bin->left, pk_col) && is_constant(bin->right))
            return std::make_pair(bin->right, bin->right);
        return std::nullopt;
    }
    if (bin->op == ">=") {
        if (is_identifier(bin->left, pk_col) && is_constant(bin->right))
            return std::make_pair(bin->right, static_cast<Expr*>(nullptr));
        return std::nullopt;
    }
    if (bin->op == ">") {
        if (is_identifier(bin->left, pk_col) && is_constant(bin->right)) {
            if (need_strict_filter) *need_strict_filter = true;
            return std::make_pair(bin->right, static_cast<Expr*>(nullptr));
        }
        return std::nullopt;
    }
    if (bin->op == "AND") {
        if (bin->left->kind != ExprKind::Binary || bin->right->kind != ExprKind::Binary)
            return std::nullopt;
        BinaryExpr* left = static_cast<BinaryExpr*>(bin->left);
        BinaryExpr* right = static_cast<BinaryExpr*>(bin->right);
        Expr* start = nullptr;
        Expr* end = nullptr;
        if (left->op == ">=" && is_identifier(left->left, pk_col) && is_constant(left->right))
            start = left->right;
        if (left->op == "<=" && is_identifier(left->left, pk_col) && is_constant(left->right))
            end = left->right;
        if (right->op == ">=" && is_identifier(right->left, pk_col) && is_constant(right->right))
            start = right->right;
        if (right->op == "<=" && is_identifier(right->left, pk_col) && is_constant(right->right))
            end = right->right;
        if (start != nullptr)
            return std::make_pair(start, end);
        return std::nullopt;
    }
    return std::nullopt;
}

// Returns true if SELECT projection includes the PK column (* or explicit pk column).
static bool select_includes_pk(const SelectStmt& select_stmt, const std::string& pk_col_name) {
    if (pk_col_name.empty()) return false;
    for (Expr* col : select_stmt.columns) {
        if (!col) continue;
        if (col->kind == ExprKind::Star) return true;  // SELECT * includes all columns
        if (col->kind == ExprKind::Identifier && static_cast<IdentifierExpr*>(col)->name == pk_col_name)
            return true;
    }
    return false;
}

// Returns true if the plan uses cursor-based scans that can be invalidated
static bool needs_collection(const Plan* plan) {
    if (!plan) return false;
    
    switch (plan->type) {
        case PlanType::SeqScan:
            return true;  // SeqScan uses cursors
        case PlanType::IndexScan:
            return true;  // IndexScan uses cursors (future)
        case PlanType::Filter: {
            // Recursively check the source
            const FilterPlan* filter = static_cast<const FilterPlan*>(plan);
            return needs_collection(filter->source.get());
        }
        case PlanType::Project: {
            // Recursively check the source
            const ProjectPlan* project = static_cast<const ProjectPlan*>(plan);
            return needs_collection(project->source.get());
        }
        case PlanType::Values:
            return false;  // Values doesn't use cursors
        case PlanType::Collect:
            return false;  // Already collected
        default:
            return false;  // Conservative: assume no cursor for unknown types
    }
}

// Build plan for SELECT statement.
// If table has PK and SELECT includes PK column, use IndexScanPlan (range or full scan).
static std::unique_ptr<Plan> build_select_plan(const SelectStmt& select_stmt,
                                               const CreateTableStmt* table_schema) {
    std::unique_ptr<Plan> plan;
    std::string pk_col_name;
    if (table_schema) {
        for (const auto& col : table_schema->columns) {
            if (col.is_primary_key) {
                pk_col_name = col.name;
                break;
            }
        }
    }
    bool use_index = select_includes_pk(select_stmt, pk_col_name);
    bool need_strict_filter = false;
    Expr* start_expr = nullptr;
    Expr* end_expr = nullptr;
    if (use_index && select_stmt.where != nullptr) {
        auto range = extract_pk_range(select_stmt.where, pk_col_name, &need_strict_filter);
        if (range.has_value()) {
            start_expr = range->first;
            end_expr = range->second;
        }
    }
    if (use_index) {
        plan = std::make_unique<IndexScanPlan>(select_stmt.table, pk_col_name, start_expr, end_expr);
        if (need_strict_filter)
            plan = std::make_unique<FilterPlan>(select_stmt.where, std::move(plan));
        else if (select_stmt.where != nullptr && start_expr == nullptr)
            plan = std::make_unique<FilterPlan>(select_stmt.where, std::move(plan));
    } else {
        plan = std::make_unique<SeqScanPlan>(select_stmt.table);
        if (select_stmt.where != nullptr)
            plan = std::make_unique<FilterPlan>(select_stmt.where, std::move(plan));
    }
    if (!select_stmt.order_by.empty()) {
        plan = std::make_unique<CollectPlan>(std::move(plan));
        plan = std::make_unique<SortPlan>(select_stmt.order_by, std::move(plan));
    }
    if (!select_stmt.columns.empty()) {
        plan = std::make_unique<ProjectPlan>(select_stmt.columns, std::move(plan));
    }
    return plan;
}

// Build plan for INSERT statement
static std::unique_ptr<Plan> build_insert_plan(const InsertStmt& insert_stmt) {
    // Step 1: Create Values plan with the values expressions
    auto values_plan = std::make_unique<ValuesPlan>(insert_stmt.values);
    
    // Step 2: Create Insert plan with table, columns, and values as source
    auto insert_plan = std::make_unique<InsertPlan>(
        insert_stmt.table,
        insert_stmt.columns,
        std::move(values_plan)
    );
    
    return insert_plan;
}

// Build plan for UPDATE statement
static std::unique_ptr<Plan> build_update_plan(const UpdateStmt& update_stmt) {
    // Step 1: Create base sequential scan
    std::unique_ptr<Plan> plan = std::make_unique<SeqScanPlan>(update_stmt.table);
    
    // Step 2: Add WHERE filter if present
    if (update_stmt.where != nullptr) {
        plan = std::make_unique<FilterPlan>(update_stmt.where, std::move(plan));
    }
    
    // Step 3: Insert Collect if source uses cursor-based scans
    // This ensures cursor safety: mutations won't invalidate the scan cursor
    if (needs_collection(plan.get())) {
        plan = std::make_unique<CollectPlan>(std::move(plan));
    }
    
    // Step 4: Create Update plan with table, assignments, and collected scan as source
    plan = std::make_unique<UpdatePlan>(
        update_stmt.table,
        update_stmt.assignments,
        std::move(plan)
    );
    
    return plan;
}

// Build plan for DELETE statement.
// If table_schema is non-null and WHERE is a range on the PK, use IndexScanPlan.
static std::unique_ptr<Plan> build_delete_plan(const DeleteStmt& delete_stmt,
                                                const CreateTableStmt* table_schema) {
    std::unique_ptr<Plan> plan;
    std::string pk_col_name;
    if (table_schema) {
        for (const auto& col : table_schema->columns) {
            if (col.is_primary_key) {
                pk_col_name = col.name;
                break;
            }
        }
    }
    bool use_index = false;
    bool need_strict_filter = false;
    Expr* start_expr = nullptr;
    Expr* end_expr = nullptr;
    if (!pk_col_name.empty() && delete_stmt.where != nullptr) {
        auto range = extract_pk_range(delete_stmt.where, pk_col_name, &need_strict_filter);
        if (range.has_value()) {
            use_index = true;
            start_expr = range->first;
            end_expr = range->second;
        }
    }
    if (use_index && start_expr != nullptr) {
        plan = std::make_unique<IndexScanPlan>(delete_stmt.table, pk_col_name, start_expr, end_expr);
        if (need_strict_filter)
            plan = std::make_unique<FilterPlan>(delete_stmt.where, std::move(plan));
    } else {
        plan = std::make_unique<SeqScanPlan>(delete_stmt.table);
        if (delete_stmt.where != nullptr)
            plan = std::make_unique<FilterPlan>(delete_stmt.where, std::move(plan));
    }
    // Insert Collect if source uses cursor-based scans
    if (needs_collection(plan.get())) {
        plan = std::make_unique<CollectPlan>(std::move(plan));
    }
    plan = std::make_unique<DeletePlan>(delete_stmt.table, std::move(plan));
    return plan;
}

// Main build_plan function that dispatches based on statement type
std::unique_ptr<Plan> build_plan(const Statement& stmt,
                                 const CreateTableStmt* table_schema) {
    switch (stmt.get_type()) {
        case StatementType::Select:
            return build_select_plan(stmt.as_select(), table_schema);

        case StatementType::Insert:
            return build_insert_plan(stmt.as_insert());
            
        case StatementType::Update:
            return build_update_plan(stmt.as_update());
            
        case StatementType::Delete:
            return build_delete_plan(stmt.as_delete(), table_schema);
            
        case StatementType::Create:
            throw std::runtime_error("CREATE statements do not require execution plans");
        case StatementType::Use:
            throw std::runtime_error("USE statements do not require execution plans");

        default:
            throw std::runtime_error("Unsupported statement type for planning");
    }
}
