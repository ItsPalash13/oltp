#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "types.h"
#include <optional>
#include <vector>
#include <map>
#include <string>

// Forward declarations
struct Plan;
struct Transaction;
class Storage;
class BPlusTree;

// Base Executor interface
// Each executor implements the iterator model with a next() method
class Executor {
public:
    virtual ~Executor() = default;
    virtual std::optional<Tuple> next() = 0;
};

// Main execution entry point. Optional btree and txn for IndexScan/Insert.
std::vector<Tuple> execute_plan(Plan* plan, Storage& storage,
                                 const std::map<std::string, std::vector<std::string>>& schema,
                                 BPlusTree* btree = nullptr,
                                 Transaction* txn = nullptr);

#endif // EXECUTOR_H
