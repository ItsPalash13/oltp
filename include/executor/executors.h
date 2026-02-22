#ifndef EXECUTORS_H
#define EXECUTORS_H

#include "executor/executor.h"
#include "executor/storage.h"
#include "executor/types.h"
#include "executor/scan_position.h"
#include "parser/statements/create.h"
#include "parser/statements/update.h"
#include <memory>
#include <vector>
#include <string>
#include <optional>

// Forward declarations
struct Expr;

// Empty executor (yields no rows; used when index search finds no position)
class EmptyExecutor : public Executor {
public:
    std::optional<Tuple> next() override { return std::nullopt; }
};

// Sequential scan executor
// Reads tuples from in-memory table or via next_row_from (executor holds ScanPosition)
class SeqScanExecutor : public Executor {
private:
    Storage* storage = nullptr;
    std::string table_name;
    size_t cursor_idx = 0;
    Table* table = nullptr;
    class BufferPoolManager* bp_ = nullptr;
    class StorageManager* sm_ = nullptr;
    std::string scan_table_name_;
    uint32_t table_id_ = 0;
    std::optional<CreateTableStmt> schema_;
    ScanPosition position_;

public:
    SeqScanExecutor(Storage& s, const std::string& table);
    SeqScanExecutor(BufferPoolManager* bp, StorageManager* sm, const std::string& table_name, uint32_t table_id, CreateTableStmt schema, ScanPosition initial_position);
    std::optional<Tuple> next() override;
};

// Index scan executor (range scan over B+ tree; executor holds ScanPosition)
class IndexScanExecutor : public Executor {
private:
    class BPlusTree* tree_ = nullptr;
    std::optional<CreateTableStmt> schema_;
    ScanPosition position_;

public:
    explicit IndexScanExecutor(BPlusTree* tree, CreateTableStmt schema, ScanPosition initial_position);
    std::optional<Tuple> next() override;
};

// Filter executor
// Applies predicate to tuples from its child
class FilterExecutor : public Executor {
private:
    std::unique_ptr<Executor> child;
    Expr* predicate;
    std::vector<std::string> column_names;  // For expression evaluation

public:
    FilterExecutor(std::unique_ptr<Executor> child, Expr* pred, 
                   const std::vector<std::string>& cols);
    std::optional<Tuple> next() override;
};

// Project executor
// Transforms input tuples into output tuples via projections
class ProjectExecutor : public Executor {
private:
    std::unique_ptr<Executor> child;
    std::vector<Expr*> projections;
    std::vector<std::string> column_names;  // Input column names for expression evaluation
    std::vector<Expr*> owned_projections_;   // When non-empty, we own these (e.g. expanded SELECT *)

public:
    ProjectExecutor(std::unique_ptr<Executor> child,
                   const std::vector<Expr*>& proj,
                   const std::vector<std::string>& cols,
                   bool own_projections = false);
    ~ProjectExecutor() override;
    std::optional<Tuple> next() override;
};

// Values executor (evaluates expression list once, produces one tuple)
class ValuesExecutor : public Executor {
private:
    std::vector<Expr*> values_;
    bool done_ = false;

public:
    explicit ValuesExecutor(const std::vector<Expr*>& values);
    std::optional<Tuple> next() override;
};

// Insert executor (consumes from child, inserts each tuple into B+ tree)
class InsertExecutor : public Executor {
private:
    std::unique_ptr<Executor> child_;
    class BPlusTree* tree_ = nullptr;
    CreateTableStmt schema_;
    std::vector<std::string> insert_columns_;  // Column list from INSERT (empty = all columns in schema order)
    struct Transaction* txn_ = nullptr;
    int pk_col_index_ = -1;
    bool inserted_ = false;
    // For each schema column: -1 = take from child at mapped index; else = AUTO_INCREMENT column index (0-based among AI columns)
    std::vector<int> schema_to_child_index_;  // schema col i -> index in child tuple, or -1 if AUTO_INCREMENT
    std::vector<int> schema_to_ai_index_;     // schema col i -> AI counter index (0-based), or -1 if not AI

public:
    InsertExecutor(std::unique_ptr<Executor> child, class BPlusTree* tree,
                  CreateTableStmt schema, std::vector<std::string> insert_columns, struct Transaction* txn);
    std::optional<Tuple> next() override;
};

// Collect executor (materializes child so mutating executors do not scan while modifying)
class CollectExecutor : public Executor {
private:
    std::unique_ptr<Executor> child_;
    std::vector<Tuple> rows_;
    size_t index_ = 0;
    bool drained_ = false;

public:
    explicit CollectExecutor(std::unique_ptr<Executor> child);
    std::optional<Tuple> next() override;
};

// Delete executor (consumes from child, removes each row by PK; returns rows affected count)
class DeleteExecutor : public Executor {
private:
    std::unique_ptr<Executor> child_;
    class BPlusTree* tree_ = nullptr;
    CreateTableStmt schema_;
    int pk_col_index_ = -1;
    bool done_ = false;

public:
    DeleteExecutor(std::unique_ptr<Executor> child, class BPlusTree* tree, CreateTableStmt schema);
    std::optional<Tuple> next() override;
};

// Update executor (delete-then-insert for each matching row)
class UpdateExecutor : public Executor {
private:
    std::unique_ptr<Executor> child_;
    class BPlusTree* tree_ = nullptr;
    CreateTableStmt schema_;
    std::vector<Assignment> assignments_;
    std::vector<std::string> column_names_;
    struct Transaction* txn_ = nullptr;
    int pk_col_index_ = -1;
    bool done_ = false;

public:
    UpdateExecutor(std::unique_ptr<Executor> child, BPlusTree* tree,
                   CreateTableStmt schema, std::vector<Assignment> assignments,
                   Transaction* txn);
    std::optional<Tuple> next() override;
};

#endif // EXECUTORS_H
