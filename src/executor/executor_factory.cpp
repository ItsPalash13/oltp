#include "executor/executor_factory.h"
#include "executor/executors.h"
#include "executor/expr_defs.h"
#include "executor/evaluator.h"
#include "executor/tuple_codec.h"
#include "executor/scan_position.h"
#include "planner/plan.h"
#include "parser/statements/create.h"
#include "storage/bplustree.h"
#include "transaction/transaction.h"
#include <stdexcept>
#include <vector>

std::unique_ptr<Executor> build_executor(Plan* plan, Storage& storage,
                                         const std::map<std::string, std::vector<std::string>>& schema,
                                         BPlusTree* btree,
                                         Transaction* txn) {
    if (!plan) {
        throw std::runtime_error("Null plan");
    }

    switch (plan->type) {
        case PlanType::Values: {
            ValuesPlan* values_plan = static_cast<ValuesPlan*>(plan);
            return std::make_unique<ValuesExecutor>(values_plan->values);
        }

        case PlanType::Insert: {
            InsertPlan* insert_plan = static_cast<InsertPlan*>(plan);
            std::unique_ptr<Executor> child = build_executor(insert_plan->source.get(), storage, schema, btree, txn);
            CreateTableStmt table_schema = storage.get_table_schema(insert_plan->table);
            return std::make_unique<InsertExecutor>(std::move(child), btree, std::move(table_schema),
                                                    insert_plan->columns, txn);
        }

        case PlanType::SeqScan: {
            SeqScanPlan* scan_plan = static_cast<SeqScanPlan*>(plan);
            CreateTableStmt table_schema = storage.get_table_schema(scan_plan->table);
            StorageManager& sm = storage.get_storage_manager(scan_plan->table);
            uint32_t table_id = storage.get_table_id(scan_plan->table);
            BufferPoolManager& bp = storage.get_buffer_pool();
            ScanPosition start{2, 0};
            return std::make_unique<SeqScanExecutor>(&bp, &sm, scan_plan->table, table_id, std::move(table_schema), start);
        }


        case PlanType::IndexScan: {
            IndexScanPlan* index_plan = static_cast<IndexScanPlan*>(plan);
            if (!btree) {
                throw std::runtime_error("IndexScan requires BPlusTree; orchestrator must create it when plan uses index");
            }
            CreateTableStmt table_schema = storage.get_table_schema(index_plan->table);
            const ColumnDef* pk_col = nullptr;
            for (const auto& c : table_schema.columns) {
                if (c.is_primary_key) { pk_col = &c; break; }
            }
            if (!pk_col) {
                throw std::runtime_error("IndexScan requires primary key");
            }
            std::optional<std::pair<uint32_t, uint16_t>> pos;
            if (index_plan->start_key_expr != nullptr) {
                Value start_val = evaluate_expr(index_plan->start_key_expr, {}, {});
                std::vector<uint8_t> key_bytes = serialize_pk_value(start_val, pk_col->data_type);
                pos = btree->search(key_bytes.data(), static_cast<uint16_t>(key_bytes.size()));
            } else {
                pos = btree->find_leftmost();
            }
            if (!pos.has_value()) {
                return std::make_unique<EmptyExecutor>();
            }
            std::vector<std::string> column_names;
            for (const auto& c : table_schema.columns)
                column_names.push_back(c.name);
            ScanPosition start;
            start.page_id = pos->first;
            start.slot_index = pos->second;
            std::unique_ptr<Executor> exec = std::make_unique<IndexScanExecutor>(btree, std::move(table_schema), start);
            if (index_plan->end_key_expr != nullptr) {
                Expr* pred = new BinaryExpr("<=", new IdentifierExpr(pk_col->name), index_plan->end_key_expr);
                exec = std::make_unique<FilterExecutor>(std::move(exec), pred, column_names);
            }
            return exec;
        }

        case PlanType::Filter: {
            FilterPlan* filter_plan = static_cast<FilterPlan*>(plan);
            std::unique_ptr<Executor> child = build_executor(filter_plan->source.get(), storage, schema, btree, txn);
            
            // Get column names for the table (needed for expression evaluation)
            // We need to find the base table from the child plan
            std::vector<std::string> column_names;
            Plan* child_plan = filter_plan->source.get();
            
            // Traverse down to find the base table
            while (child_plan) {
                if (child_plan->type == PlanType::SeqScan) {
                    SeqScanPlan* scan = static_cast<SeqScanPlan*>(child_plan);
                    auto it = schema.find(scan->table);
                    if (it != schema.end()) column_names = it->second;
                    break;
                } else if (child_plan->type == PlanType::IndexScan) {
                    IndexScanPlan* scan = static_cast<IndexScanPlan*>(child_plan);
                    auto it = schema.find(scan->table);
                    if (it != schema.end()) column_names = it->second;
                    break;
                } else if (child_plan->type == PlanType::Filter) {
                    FilterPlan* filter = static_cast<FilterPlan*>(child_plan);
                    child_plan = filter->source.get();
                } else if (child_plan->type == PlanType::Project) {
                    ProjectPlan* project = static_cast<ProjectPlan*>(child_plan);
                    child_plan = project->source.get();
                } else {
                    break;
                }
            }
            return std::make_unique<FilterExecutor>(std::move(child), filter_plan->predicate, column_names);
        }

        case PlanType::Project: {
            ProjectPlan* project_plan = static_cast<ProjectPlan*>(plan);
            std::unique_ptr<Executor> child = build_executor(project_plan->source.get(), storage, schema, btree, txn);
            
            // Get column names for the input (needed for expression evaluation)
            std::vector<std::string> column_names;
            Plan* child_plan = project_plan->source.get();
            
            // Traverse down to find the base table
            while (child_plan) {
                if (child_plan->type == PlanType::SeqScan) {
                    SeqScanPlan* scan = static_cast<SeqScanPlan*>(child_plan);
                    auto it = schema.find(scan->table);
                    if (it != schema.end()) column_names = it->second;
                    break;
                } else if (child_plan->type == PlanType::IndexScan) {
                    IndexScanPlan* scan = static_cast<IndexScanPlan*>(child_plan);
                    auto it = schema.find(scan->table);
                    if (it != schema.end()) column_names = it->second;
                    break;
                } else if (child_plan->type == PlanType::Filter) {
                    FilterPlan* filter = static_cast<FilterPlan*>(child_plan);
                    child_plan = filter->source.get();
                } else if (child_plan->type == PlanType::Project) {
                    ProjectPlan* project = static_cast<ProjectPlan*>(child_plan);
                    child_plan = project->source.get();
                } else {
                    break;
                }
            }
            // Expand SELECT * to all columns
            const std::vector<Expr*>& proj = project_plan->projections;
            if (proj.size() == 1 && proj[0]->kind == ExprKind::Star) {
                std::vector<Expr*> expanded;
                for (const std::string& col : column_names) {
                    expanded.push_back(new IdentifierExpr(col));
                }
                return std::make_unique<ProjectExecutor>(std::move(child), expanded, column_names, true);
            }
            return std::make_unique<ProjectExecutor>(std::move(child), project_plan->projections, column_names);
        }

        case PlanType::Collect: {
            CollectPlan* collect_plan = static_cast<CollectPlan*>(plan);
            std::unique_ptr<Executor> child = build_executor(collect_plan->source.get(), storage, schema, btree, txn);
            return std::make_unique<CollectExecutor>(std::move(child));
        }

        case PlanType::Delete: {
            DeletePlan* delete_plan = static_cast<DeletePlan*>(plan);
            if (!btree) {
                throw std::runtime_error("Delete requires BPlusTree; orchestrator must create it for DELETE");
            }
            std::unique_ptr<Executor> child = build_executor(delete_plan->source.get(), storage, schema, btree, txn);
            CreateTableStmt table_schema = storage.get_table_schema(delete_plan->table);
            return std::make_unique<DeleteExecutor>(std::move(child), btree, std::move(table_schema));
        }

        case PlanType::Update: {
            UpdatePlan* update_plan = static_cast<UpdatePlan*>(plan);
            if (!btree) {
                throw std::runtime_error("Update requires BPlusTree; orchestrator must create it for UPDATE");
            }
            std::unique_ptr<Executor> child = build_executor(update_plan->source.get(), storage, schema, btree, txn);
            CreateTableStmt table_schema = storage.get_table_schema(update_plan->table);
            return std::make_unique<UpdateExecutor>(std::move(child), btree, std::move(table_schema),
                                                   update_plan->assignments, txn);
        }

        default:
            throw std::runtime_error("Unsupported plan type in executor factory");
    }
}
