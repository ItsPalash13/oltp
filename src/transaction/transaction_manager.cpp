#include "transaction/transaction_manager.h"
#include <utility>

TransactionManager::TransactionManager()
    : next_txn_id_(1),
      completed_txn_id_(0),
      running_(false),
      shutdown_(false) {
    worker_ = std::thread(&TransactionManager::worker_loop, this);
}

TransactionManager::~TransactionManager() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        shutdown_ = true;
        worker_cv_.notify_one();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TransactionManager::execute(std::function<void(Transaction&)> fn) {
    Transaction txn{next_txn_id_++};
    {
        std::lock_guard<std::mutex> lock(mtx_);
        exception_ = nullptr;  // Clear any previous exception
        queue_.emplace(txn, std::move(fn));
        worker_cv_.notify_one();
    }
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this, &txn] { return completed_txn_id_ >= txn.txn_id; });
        // Re-throw exception if one occurred
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }
}

void TransactionManager::worker_loop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            worker_cv_.wait(lock, [this] {
                return !queue_.empty() || shutdown_;
            });
            if (shutdown_ && queue_.empty()) {
                break;
            }
            if (queue_.empty()) {
                continue;
            }
            task = std::move(queue_.front());
            queue_.pop();
            running_ = true;
        }
        
        // Execute task and catch any exceptions
        try {
            task.second(task.first);
        } catch (...) {
            // Store the exception to be re-thrown in execute()
            std::lock_guard<std::mutex> lock(mtx_);
            exception_ = std::current_exception();
            completed_txn_id_ = task.first.txn_id;
            running_ = false;
            cv_.notify_all();
            continue;
        }
        
        {
            std::lock_guard<std::mutex> lock(mtx_);
            completed_txn_id_ = task.first.txn_id;
            running_ = false;
            cv_.notify_all();
        }
    }
}
