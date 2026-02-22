#ifndef EXECUTOR_FACTORY_H
#define EXECUTOR_FACTORY_H

#include "executor/executor.h"
#include "executor/storage.h"
#include <memory>
#include <map>
#include <vector>
#include <string>

// Forward declarations
struct Plan;
struct Transaction;
class BPlusTree;

// Build executor tree from plan tree
// Recursively creates executors for each plan node.
// For Insert: pass non-null btree and txn so InsertExecutor can write to B+ tree.
std::unique_ptr<Executor> build_executor(Plan* plan, Storage& storage,
                                         const std::map<std::string, std::vector<std::string>>& schema,
                                         BPlusTree* btree = nullptr,
                                         Transaction* txn = nullptr);

#endif // EXECUTOR_FACTORY_H
