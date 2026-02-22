/**
 * Central orchestrator: parse -> analyse -> plan (if DML) -> execute -> write to out/err.
 * Statement work runs inside TransactionManager::execute (one transaction at a time).
 */
#include "orchestrator/orchestrator.h"
#include "../parser/statements.cpp"
#include "analyser/analyser.h"
#include "planner/planner.h"
#include "planner/plan.h"
#include "executor/executor.h"
#include "executor/executor_factory.h"
#include "executor/storage.h"
#include "executor/types.h"
#include "storage/bplustree.h"
#include "storage/db_manager.h"
#include "transaction/transaction_manager.h"
#include "transaction/transaction.h"
#include "parser/statements/statement.h"
#include "parser/statements/create.h"
#include "parser/statements/use.h"
#include "parser/statements/describe.h"
#include "parser/statements/catalog.h"
#include <map>
#include <memory>
#include <stdexcept>

static void print_tuple(std::ostream& out, const std::vector<Value>& tuple) {
    out << "[";
    for (size_t i = 0; i < tuple.size(); i++) {
        if (i > 0) out << ", ";
        if (std::holds_alternative<int>(tuple[i])) {
            out << std::get<int>(tuple[i]);
        } else if (std::holds_alternative<std::string>(tuple[i])) {
            out << "\"" << std::get<std::string>(tuple[i]) << "\"";
        }
    }
    out << "]";
}

static void print_results(std::ostream& out, const std::vector<Tuple>& results) {
    out << "Results (" << results.size() << " rows):\n";
    for (const auto& tuple : results) {
        print_tuple(out, tuple);
        out << "\n";
    }
}

static std::string get_table_from_statement(const Statement& stmt) {
    switch (stmt.get_type()) {
        case StatementType::Select: return stmt.as_select().table;
        case StatementType::Insert: return stmt.as_insert().table;
        case StatementType::Update: return stmt.as_update().table;
        case StatementType::Delete: return stmt.as_delete().table;
        default: return "";
    }
}

static bool plan_uses_index_scan(Plan* plan) {
    if (!plan) return false;
    if (plan->type == PlanType::IndexScan) return true;
    switch (plan->type) {
        case PlanType::Filter:
            return plan_uses_index_scan(static_cast<FilterPlan*>(plan)->source.get());
        case PlanType::Project:
            return plan_uses_index_scan(static_cast<ProjectPlan*>(plan)->source.get());
        case PlanType::Sort:
            return plan_uses_index_scan(static_cast<SortPlan*>(plan)->source.get());
        case PlanType::Collect:
            return plan_uses_index_scan(static_cast<CollectPlan*>(plan)->source.get());
        default:
            return false;
    }
}

void run_query(const std::string& sql, DatabaseManager& db_mgr,
               TransactionManager& txn_mgr, std::ostream& out, std::ostream& err) {
    (void)err;  // Used by caller for top-level catch; pipeline throws
    Parser parser(sql);
    Statement stmt = parse_statement(parser);

    std::string db_path = db_mgr.get_current_db_path();
    AnalysisResult result = analyse(stmt, db_mgr, db_path);

    Storage* engine = db_mgr.get_storage_engine();

    txn_mgr.execute([&](Transaction& txn) {
        switch (stmt.get_type()) {
            case StatementType::Create: {
                const CreateStmt& create_stmt = stmt.as_create();
                if (create_stmt.is_database() && result.create_database_name) {
                    out << "Database created: " << *result.create_database_name << "\n";
                } else if (create_stmt.is_table() && result.create_table_stmt) {
                    const CreateTableStmt& table_stmt = *result.create_table_stmt;
                    out << "Table created: " << table_stmt.table_name << "\n";
                    for (const auto& col : table_stmt.columns) {
                        out << "  - " << col.name << " " << col.data_type << "\n";
                    }
                }
                break;
            }

            case StatementType::Use: {
                if (result.use_database_name) {
                    out << "Using database: " << *result.use_database_name << "\n";
                }
                break;
            }

            case StatementType::Describe: {
                if (result.describe_schema) {
                    const DescribeStmt& describe_stmt = stmt.as_describe();
                    out << "Table: " << describe_stmt.table_name << "\n";
                    for (const auto& col : result.describe_schema->columns) {
                        out << "  - " << col.name << " " << col.data_type << "\n";
                    }
                }
                break;
            }

            case StatementType::Catalog: {
                const CatalogStmt& catalog_stmt = stmt.as_catalog();
                switch (catalog_stmt.op) {
                    case CatalogOp::List:
                        if (result.catalog_list) {
                            out << "Cached tables (" << result.catalog_list->size() << "):\n";
                            for (const auto& p : *result.catalog_list) {
                                out << "  - " << p.first << " (" << p.second << ")\n";
                            }
                        }
                        break;
                    case CatalogOp::Read:
                        if (result.catalog_read_table) {
                            out << "Loaded table into cache: " << *result.catalog_read_table << "\n";
                        }
                        break;
                    case CatalogOp::View:
                        if (result.catalog_view) {
                            out << "Catalog cache (" << result.catalog_view->size() << " slots used):\n";
                            out << "  slot  table       db_path              last_access  dirty\n";
                            for (const auto& e : *result.catalog_view) {
                                out << "  " << e.slot << "     " << e.table_name << "  " << e.db_path << "  " << e.last_access_time << "  " << (e.dirty ? "yes" : "no") << "\n";
                            }
                        }
                        break;
                    case CatalogOp::Evict:
                        if (result.catalog_evict_table) {
                            out << "Evicted from cache: " << *result.catalog_evict_table << "\n";
                        }
                        break;
                }
                break;
            }

            case StatementType::Select: {
                if (result.select_schema) {
                    if (!engine) {
                        out << "Error: No database selected. Use USE <db>; first.\n";
                        break;
                    }
                    std::string table_name = get_table_from_statement(stmt);
                    CreateTableStmt table_schema = engine->get_table_schema(table_name);
                    std::unique_ptr<Plan> plan = build_plan(stmt, &table_schema);
                    std::vector<Tuple> results;
                    if (plan_uses_index_scan(plan.get())) {
                        std::unique_ptr<BPlusTree> btree = engine->create_bplustree(table_name);
                        results = execute_plan(plan.get(), *engine, *result.select_schema, btree.get(), nullptr);
                    } else {
                        results = execute_plan(plan.get(), *engine, *result.select_schema);
                    }
                    print_results(out, results);
                }
                break;
            }

            case StatementType::Insert: {
                if (!engine) {
                    out << "Error: No database selected. Use USE <db>; first.\n";
                    break;
                }
                std::unique_ptr<Plan> plan = build_plan(stmt);
                std::string table_name = get_table_from_statement(stmt);
                std::unique_ptr<BPlusTree> btree = engine->create_bplustree(table_name);
                std::map<std::string, std::vector<std::string>> schema;
                std::unique_ptr<Executor> executor = build_executor(plan.get(), *engine, schema, btree.get(), &txn);
                std::optional<Tuple> result = executor->next();
                if (result.has_value() && !result->empty() && std::holds_alternative<int>(result->front())) {
                    out << "Rows inserted: " << std::get<int>(result->front()) << "\n";
                } else {
                    out << "Rows inserted: 0\n";
                }
                engine->get_catalog().flush();
                break;
            }

            case StatementType::Delete: {
                if (!engine) {
                    out << "Error: No database selected. Use USE <db>; first.\n";
                    break;
                }
                std::string table_name = get_table_from_statement(stmt);
                CreateTableStmt table_schema = engine->get_table_schema(table_name);
                std::unique_ptr<Plan> plan = build_plan(stmt, &table_schema);
                std::unique_ptr<BPlusTree> btree = engine->create_bplustree(table_name);
                std::map<std::string, std::vector<std::string>> schema;
                std::vector<std::string> column_names;
                for (const auto& c : table_schema.columns)
                    column_names.push_back(c.name);
                schema[table_name] = column_names;
                std::unique_ptr<Executor> executor = build_executor(plan.get(), *engine, schema, btree.get(), &txn);
                std::optional<Tuple> result = executor->next();
                if (result.has_value() && !result->empty() && std::holds_alternative<int>(result->front())) {
                    out << "Rows deleted: " << std::get<int>(result->front()) << "\n";
                } else {
                    out << "Rows deleted: 0\n";
                }
                engine->get_catalog().flush();
                break;
            }

            case StatementType::Update: {
                if (!engine) {
                    out << "Error: No database selected. Use USE <db>; first.\n";
                    break;
                }
                std::string table_name = get_table_from_statement(stmt);
                CreateTableStmt table_schema = engine->get_table_schema(table_name);
                std::unique_ptr<Plan> plan = build_plan(stmt, &table_schema);
                std::unique_ptr<BPlusTree> btree = engine->create_bplustree(table_name);
                std::map<std::string, std::vector<std::string>> schema;
                std::vector<std::string> column_names;
                for (const auto& c : table_schema.columns)
                    column_names.push_back(c.name);
                schema[table_name] = column_names;
                std::unique_ptr<Executor> executor = build_executor(plan.get(), *engine, schema, btree.get(), &txn);
                std::optional<Tuple> result = executor->next();
                if (result.has_value() && !result->empty() && std::holds_alternative<int>(result->front())) {
                    out << "Rows updated: " << std::get<int>(result->front()) << "\n";
                } else {
                    out << "Rows updated: 0\n";
                }
                engine->get_catalog().flush();
                break;
            }
        }
    });
}
