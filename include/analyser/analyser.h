#ifndef ANALYSER_H
#define ANALYSER_H

#include "parser/statements/create.h"
#include "storage/catalog_manager.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

class Statement;
class DatabaseManager;

/**
 * Result of analysis: data the orchestrator needs for output/execution.
 * Analyser performs all catalog/db side effects (CREATE, USE, CATALOG ops)
 * and returns only the data needed to print or run the plan.
 */
struct AnalysisResult {
    // DESCRIBE: schema to print (single table)
    std::optional<CreateTableStmt> describe_schema;

    // SELECT: schema for execution (table_name -> column names)
    std::optional<std::map<std::string, std::vector<std::string>>> select_schema;

    // CATALOG LIST
    std::optional<std::vector<std::pair<std::string, std::string>>> catalog_list;
    // CATALOG VIEW
    std::optional<std::vector<CatalogCacheEntry>> catalog_view;
    // CATALOG READ / EVICT: table name for message
    std::optional<std::string> catalog_read_table;
    std::optional<std::string> catalog_evict_table;

    // DDL performed by analyser (orchestrator only prints message)
    std::optional<std::string> create_database_name;
    std::optional<CreateTableStmt> create_table_stmt;
    std::optional<std::string> use_database_name;
};

/**
 * Analyse statement and perform catalog/db side effects for DDL and CATALOG.
 * Catalog is obtained from db_mgr.get_storage_engine()->get_catalog() when needed.
 *
 * @param stmt Parsed statement
 * @param db_mgr Database manager (USE, CREATE DATABASE, get_storage_engine, get_current_db_path)
 * @param db_path Current database path (from DatabaseManager::get_current_db_path())
 * @return AnalysisResult with describe_schema, select_schema, catalog_* or DDL names for printing
 */
AnalysisResult analyse(const Statement& stmt, DatabaseManager& db_mgr, const std::string& db_path);

#endif // ANALYSER_H
