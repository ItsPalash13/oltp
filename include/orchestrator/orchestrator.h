#ifndef ORCHESTRATOR_H
#define ORCHESTRATOR_H

#include <iostream>
#include <string>

class DatabaseManager;
class TransactionManager;

/**
 * Run the full query pipeline: parse -> analyse -> plan (when DML) -> execute.
 * Statement work runs inside TransactionManager::execute (one transaction at a time).
 * Storage engine is obtained from db_mgr (created on USE db).
 *
 * @param sql SQL statement string
 * @param db_mgr Database manager (root @data/); provides storage engine for current db
 * @param txn_mgr Transaction manager (serializes statement execution)
 * @param out Stream for result output (e.g. "Database created", "Results (...)")
 * @param err Stream for error messages
 */
void run_query(const std::string& sql, DatabaseManager& db_mgr,
               TransactionManager& txn_mgr, std::ostream& out, std::ostream& err);

#endif // ORCHESTRATOR_H
