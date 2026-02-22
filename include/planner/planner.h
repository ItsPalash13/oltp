#ifndef PLANNER_H
#define PLANNER_H

#include "planner/plan.h"
#include "parser/statements/statement.h"
#include "parser/statements/create.h"
#include <memory>
#include <utility>
#include <optional>

// Main function to build a plan from a parsed statement.
// For SELECT, pass table_schema so the planner can choose IndexScan when WHERE is a PK range.
std::unique_ptr<Plan> build_plan(const Statement& stmt,
                                  const CreateTableStmt* table_schema = nullptr);

#endif // PLANNER_H
