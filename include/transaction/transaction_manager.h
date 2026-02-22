#ifndef TRANSACTION_MANAGER_H
#define TRANSACTION_MANAGER_H

#include "transaction/transaction.h"
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstdint>
#include <exception>

/**
 * TransactionManager - one transaction at a time, blocking execute().
 * Pending transactions wait in a queue; a dedicated worker thread runs them serially.
 * execute(fn) blocks until the enqueued fn has completed.
 */
class TransactionManager {
public:
    TransactionManager();
    ~TransactionManager();

    TransactionManager(const TransactionManager&) = delete;
    TransactionManager& operator=(const TransactionManager&) = delete;

    /**
     * Enqueue work and block until this transaction has completed.
     * @param fn Work to run with a Transaction context
     */
    void execute(std::function<void(Transaction&)> fn);

private:
    using Task = std::pair<Transaction, std::function<void(Transaction&)>>;

    std::queue<Task> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::condition_variable worker_cv_;
    uint64_t next_txn_id_;
    uint64_t completed_txn_id_;
    bool running_;
    bool shutdown_;
    std::thread worker_;
    std::exception_ptr exception_;  // Store exception from worker thread

    void worker_loop();
};

#endif // TRANSACTION_MANAGER_H
