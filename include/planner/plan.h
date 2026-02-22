#ifndef PLAN_H
#define PLAN_H

#include <string>
#include <vector>
#include <memory>

// Forward declarations
struct Expr;

// Include Assignment definition (needed for UpdatePlan)
#include "parser/statements/update.h"

// Plan type enum
enum class PlanType {
    SeqScan,
    IndexScan,      // Reserved for future use
    Filter,
    Project,
    Sort,
    Insert,
    Update,
    Delete,
    Collect,        // Materialization barrier
    Values
};

// Base Plan struct
struct Plan {
    PlanType type;
    
    Plan(PlanType t) : type(t) {}
    virtual ~Plan() = default;
};

// Sequential scan plan node
struct SeqScanPlan : Plan {
    std::string table;
    
    SeqScanPlan(const std::string& table_name) 
        : Plan(PlanType::SeqScan), table(table_name) {}
};

// Index scan plan node (range scan on primary key)
struct IndexScanPlan : Plan {
    std::string table;
    std::string index_name;
    Expr* start_key_expr;  // Lower bound (e.g. constant for id >= 5); required for seek
    Expr* end_key_expr;    // Optional upper bound (e.g. for id <= 10 or BETWEEN)

    IndexScanPlan(const std::string& table_name, const std::string& idx_name,
                  Expr* start_expr = nullptr, Expr* end_expr = nullptr)
        : Plan(PlanType::IndexScan), table(table_name), index_name(idx_name),
          start_key_expr(start_expr), end_key_expr(end_expr) {}
};

// Filter plan node
struct FilterPlan : Plan {
    Expr* predicate;
    std::unique_ptr<Plan> source;
    
    FilterPlan(Expr* pred, std::unique_ptr<Plan> src)
        : Plan(PlanType::Filter), predicate(pred), source(std::move(src)) {}
};

// Project plan node
struct ProjectPlan : Plan {
    std::vector<Expr*> projections;
    std::unique_ptr<Plan> source;
    
    ProjectPlan(const std::vector<Expr*>& proj, std::unique_ptr<Plan> src)
        : Plan(PlanType::Project), projections(proj), source(std::move(src)) {}
};

// Sort plan node
struct SortPlan : Plan {
    std::vector<Expr*> order_by;
    std::unique_ptr<Plan> source;
    
    SortPlan(const std::vector<Expr*>& order, std::unique_ptr<Plan> src)
        : Plan(PlanType::Sort), order_by(order), source(std::move(src)) {}
};

// Insert plan node
struct InsertPlan : Plan {
    std::string table;
    std::vector<std::string> columns;
    std::unique_ptr<Plan> source;  // Values or Select
    
    InsertPlan(const std::string& table_name, 
               const std::vector<std::string>& cols,
               std::unique_ptr<Plan> src)
        : Plan(PlanType::Insert), table(table_name), columns(cols), source(std::move(src)) {}
};

// Update plan node
struct UpdatePlan : Plan {
    std::string table;
    std::vector<Assignment> assignments;
    std::unique_ptr<Plan> source;  // scan + filter
    
    UpdatePlan(const std::string& table_name,
               const std::vector<Assignment>& assigns,
               std::unique_ptr<Plan> src)
        : Plan(PlanType::Update), table(table_name), assignments(assigns), source(std::move(src)) {}
};

// Delete plan node
struct DeletePlan : Plan {
    std::string table;
    std::unique_ptr<Plan> source;  // scan + filter
    
    DeletePlan(const std::string& table_name, std::unique_ptr<Plan> src)
        : Plan(PlanType::Delete), table(table_name), source(std::move(src)) {}
};

// Collect plan node (materialization barrier)
struct CollectPlan : Plan {
    std::unique_ptr<Plan> source;  // Child plan to materialize
    
    CollectPlan(std::unique_ptr<Plan> src)
        : Plan(PlanType::Collect), source(std::move(src)) {}
};

// Values plan node
struct ValuesPlan : Plan {
    std::vector<Expr*> values;
    
    ValuesPlan(const std::vector<Expr*>& vals)
        : Plan(PlanType::Values), values(vals) {}
};

#endif // PLAN_H
