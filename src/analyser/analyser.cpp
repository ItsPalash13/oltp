#include "analyser/analyser.h"
#include "analyser/expr_utils.h"
#include "parser/statements/statement.h"
#include "parser/statements/create.h"
#include "parser/statements/describe.h"
#include "parser/statements/catalog.h"
#include "parser/statements/select.h"
#include "storage/db_manager.h"
#include "storage/catalog_manager.h"
#include "storage/index_policy.h"
#include "storage/page/page.h"
#include "executor/storage.h"
#include <stdexcept>
#include <filesystem>
#include <algorithm>

static bool column_in_schema(const CreateTableStmt& schema, const std::string& col_name) {
    for (const auto& col : schema.columns) {
        if (col.name == col_name) return true;
    }
    return false;
}

AnalysisResult analyse(const Statement& stmt, DatabaseManager& db_mgr, const std::string& db_path) {
    AnalysisResult result;

    switch (stmt.get_type()) {
        case StatementType::Use: {
            const UseStmt& use_stmt = stmt.as_use();
            if (!db_mgr.database_exists(use_stmt.database_name)) {
                throw std::runtime_error("Database '" + use_stmt.database_name + "' does not exist");
            }
            db_mgr.use_db(use_stmt.database_name);
            result.use_database_name = use_stmt.database_name;
            break;
        }

        case StatementType::Create: {
            const CreateStmt& create_stmt = stmt.as_create();
            if (create_stmt.is_database()) {
                const CreateDatabaseStmt& db_stmt = create_stmt.as_database();
                if (db_mgr.database_exists(db_stmt.database_name)) {
                    throw std::runtime_error("Database '" + db_stmt.database_name + "' already exists");
                }
                db_mgr.create_db(db_stmt.database_name);
                result.create_database_name = db_stmt.database_name;
            } else if (create_stmt.is_table()) {
                const CreateTableStmt& table_stmt = create_stmt.as_table();
                if (db_path.empty()) {
                    throw std::runtime_error("No database selected. Use USE <db>; first.");
                }
                Storage* eng = db_mgr.get_storage_engine();
                if (!eng) {
                    throw std::runtime_error("No database selected. Use USE <db>; first.");
                }
                CatalogManager& catalog = eng->get_catalog();
                std::string table_path = db_path + table_stmt.table_name + ".ibd";
                if (std::filesystem::exists(table_path)) {
                    throw std::runtime_error("Table '" + table_stmt.table_name + "' already exists");
                }
                if (table_stmt.columns.empty()) {
                    throw std::runtime_error("CREATE TABLE must have at least one column");
                }
                for (size_t i = 0; i < table_stmt.columns.size(); ++i) {
                    for (size_t j = i + 1; j < table_stmt.columns.size(); ++j) {
                        if (table_stmt.columns[i].name == table_stmt.columns[j].name) {
                            throw std::runtime_error("Duplicate column name '" + table_stmt.columns[i].name + "' in CREATE TABLE");
                        }
                    }
                }
                // IndexPolicy: only one PRIMARY KEY, no composite, no UNIQUE (secondary index)
                int pk_count = 0;
                for (const auto& col : table_stmt.columns) {
                    if (col.is_primary_key) pk_count++;
                    if (col.is_unique) {
                        throw std::runtime_error("UNIQUE constraint is not supported. Only primary index is allowed.");
                    }
                }
                if (pk_count > 1) {
                    throw std::runtime_error("At most one PRIMARY KEY column is allowed. Composite primary keys are not supported.");
                }
                int ai_count = 0;
                for (const auto& col : table_stmt.columns) {
                    if (col.is_auto_increment) ai_count++;
                }
                if (ai_count > static_cast<int>(PAGE0_AI_COUNTER_COUNT)) {
                    throw std::runtime_error("At most " + std::to_string(PAGE0_AI_COUNTER_COUNT) + " AUTO_INCREMENT columns are allowed per table");
                }
                // pk_count == 0 is now allowed; row_id will be used as key
                catalog.create_table_meta(db_path, table_stmt.table_name, table_stmt);
                result.create_table_stmt = table_stmt;
            }
            break;
        }

        case StatementType::Select: {
            const SelectStmt& select_stmt = stmt.as_select();
            if (db_path.empty()) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            Storage* eng = db_mgr.get_storage_engine();
            if (!eng) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            CatalogManager& catalog = eng->get_catalog();
            CreateTableStmt schema = catalog.read_schema(db_path, select_stmt.table);
            std::vector<std::string> refs;
            for (Expr* expr : select_stmt.columns) {
                refs.clear();
                collect_column_refs(expr, refs);
                validate_columns_exist(refs, schema, select_stmt.table);
            }
            if (select_stmt.where) {
                refs.clear();
                collect_column_refs(select_stmt.where, refs);
                validate_columns_exist(refs, schema, select_stmt.table);
            }
            for (Expr* expr : select_stmt.order_by) {
                refs.clear();
                collect_column_refs(expr, refs);
                validate_columns_exist(refs, schema, select_stmt.table);
            }
            for (Expr* expr : select_stmt.group_by) {
                refs.clear();
                collect_column_refs(expr, refs);
                validate_columns_exist(refs, schema, select_stmt.table);
            }
            std::map<std::string, std::vector<std::string>> schema_map;
            std::vector<std::string> cols;
            for (const auto& c : schema.columns) cols.push_back(c.name);
            schema_map[select_stmt.table] = cols;
            result.select_schema = schema_map;
            break;
        }

        case StatementType::Insert: {
            const InsertStmt& insert_stmt = stmt.as_insert();
            if (db_path.empty()) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            Storage* eng = db_mgr.get_storage_engine();
            if (!eng) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            CatalogManager& catalog = eng->get_catalog();
            CreateTableStmt schema = catalog.read_schema(db_path, insert_stmt.table);
            if (!insert_stmt.columns.empty()) {
                for (const std::string& col : insert_stmt.columns) {
                    if (!column_in_schema(schema, col)) {
                        throw std::runtime_error("Column '" + col + "' not found in table '" + insert_stmt.table + "'");
                    }
                }
                if (insert_stmt.values.size() != insert_stmt.columns.size()) {
                    throw std::runtime_error("INSERT: number of values (" + std::to_string(insert_stmt.values.size()) +
                        ") does not match number of columns (" + std::to_string(insert_stmt.columns.size()) + ")");
                }
                // Primary key must be in column list when explicit, unless it is AUTO_INCREMENT
                std::string pk_col;
                bool pk_is_auto_increment = false;
                for (const auto& c : schema.columns) {
                    if (c.is_primary_key) {
                        pk_col = c.name;
                        pk_is_auto_increment = c.is_auto_increment;
                        break;
                    }
                }
                if (!pk_col.empty() && !pk_is_auto_increment) {
                    bool pk_in_list = false;
                    for (const std::string& col : insert_stmt.columns) {
                        if (col == pk_col) {
                            pk_in_list = true;
                            break;
                        }
                    }
                    if (!pk_in_list) {
                        throw std::runtime_error("INSERT must include primary key column '" + pk_col + "'");
                    }
                }
            } else {
                if (insert_stmt.values.size() != schema.columns.size()) {
                    throw std::runtime_error("INSERT: number of values (" + std::to_string(insert_stmt.values.size()) +
                        ") does not match number of columns (" + std::to_string(schema.columns.size()) + ")");
                }
            }
            break;
        }

        case StatementType::Update: {
            const UpdateStmt& update_stmt = stmt.as_update();
            if (db_path.empty()) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            Storage* eng = db_mgr.get_storage_engine();
            if (!eng) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            CatalogManager& catalog = eng->get_catalog();
            CreateTableStmt schema = catalog.read_schema(db_path, update_stmt.table);
            for (const auto& assign : update_stmt.assignments) {
                if (!column_in_schema(schema, assign.column)) {
                    throw std::runtime_error("Column '" + assign.column + "' not found in table '" + update_stmt.table + "'");
                }
            }
            if (update_stmt.where) {
                std::vector<std::string> refs;
                collect_column_refs(update_stmt.where, refs);
                validate_columns_exist(refs, schema, update_stmt.table);
            }
            for (const auto& assign : update_stmt.assignments) {
                if (assign.value) {
                    std::vector<std::string> refs;
                    collect_column_refs(assign.value, refs);
                    validate_columns_exist(refs, schema, update_stmt.table);
                }
            }
            break;
        }

        case StatementType::Delete: {
            const DeleteStmt& delete_stmt = stmt.as_delete();
            if (db_path.empty()) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            Storage* eng = db_mgr.get_storage_engine();
            if (!eng) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            CatalogManager& catalog = eng->get_catalog();
            CreateTableStmt schema = catalog.read_schema(db_path, delete_stmt.table);
            if (delete_stmt.where) {
                std::vector<std::string> refs;
                collect_column_refs(delete_stmt.where, refs);
                validate_columns_exist(refs, schema, delete_stmt.table);
            }
            break;
        }

        case StatementType::Describe: {
            const DescribeStmt& describe_stmt = stmt.as_describe();
            if (db_path.empty()) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            Storage* eng = db_mgr.get_storage_engine();
            if (!eng) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            CatalogManager& catalog = eng->get_catalog();
            CreateTableStmt schema = catalog.read_schema(db_path, describe_stmt.table_name);
            result.describe_schema = schema;
            break;
        }

        case StatementType::Catalog: {
            const CatalogStmt& catalog_stmt = stmt.as_catalog();
            Storage* eng = db_mgr.get_storage_engine();
            if (!eng) {
                throw std::runtime_error("No database selected. Use USE <db>; first.");
            }
            CatalogManager& catalog = eng->get_catalog();
            switch (catalog_stmt.op) {
                case CatalogOp::List:
                    result.catalog_list = catalog.list_cached_tables();
                    break;
                case CatalogOp::Read: {
                    if (db_path.empty()) {
                        throw std::runtime_error("No database selected. Use USE <db>; first.");
                    }
                    (void)catalog.get_table_meta(db_path, catalog_stmt.table_name);
                    result.catalog_read_table = catalog_stmt.table_name;
                    break;
                }
                case CatalogOp::View:
                    result.catalog_view = catalog.view_cache();
                    break;
                case CatalogOp::Evict:
                    catalog.evict_table(catalog_stmt.table_name);
                    result.catalog_evict_table = catalog_stmt.table_name;
                    break;
            }
            break;
        }
    }

    return result;
}
