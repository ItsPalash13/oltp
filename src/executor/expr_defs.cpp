#include "executor/expr_defs.h"
#include <string>

IdentifierExpr::IdentifierExpr(const std::string& n) : name(n) {
    kind = ExprKind::Identifier;
}

NumberExpr::NumberExpr(int v) : value(v) {
    kind = ExprKind::Number;
}

StringExpr::StringExpr(const std::string& v) : value(v) {
    kind = ExprKind::String;
}

BinaryExpr::BinaryExpr(const std::string& o, Expr* l, Expr* r)
    : op(o), left(l), right(r) {
    kind = ExprKind::Binary;
}

StarExpr::StarExpr() {
    kind = ExprKind::Star;
}
