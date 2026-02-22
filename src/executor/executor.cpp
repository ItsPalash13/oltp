#include "executor/executor.h"
#include "executor/executor_factory.h"
#include "executor/storage.h"
#include "storage/bplustree.h"
#include "transaction/transaction.h"
#include "planner/plan.h"
#include <vector>

std::vector<Tuple> execute_plan(Plan* plan, Storage& storage,
                                 const std::map<std::string, std::vector<std::string>>& schema,
                                 BPlusTree* btree,
                                 Transaction* txn) {
    std::unique_ptr<Executor> root_executor = build_executor(plan, storage, schema, btree, txn);
    std::vector<Tuple> results;
    while (true) {
        std::optional<Tuple> tuple = root_executor->next();
        if (!tuple.has_value()) break;
        results.push_back(tuple.value());
    }
    return results;
}
