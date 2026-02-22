#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <cstdint>

/**
 * Transaction - per-transaction context.
 * Future: lock set, undo log, etc.
 */
struct Transaction {
    uint64_t txn_id;
};

#endif // TRANSACTION_H
