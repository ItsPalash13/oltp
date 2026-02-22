#ifndef EVALUATOR_H
#define EVALUATOR_H

#include "executor/types.h"
#include <vector>
#include <string>

// Forward declaration
struct Expr;

// Evaluate an expression against a tuple
// Returns the Value result of the expression
Value evaluate_expr(Expr* expr, const Tuple& tuple, 
                    const std::vector<std::string>& column_names);

// Evaluate a predicate expression against a tuple
// Returns true if the predicate is satisfied, false otherwise
bool evaluate_predicate(Expr* predicate, const Tuple& tuple,
                        const std::vector<std::string>& column_names);

#endif // EVALUATOR_H
