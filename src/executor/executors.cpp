#include "executor/executors.h"
#include "executor/evaluator.h"
#include "executor/expr_defs.h"
#include "executor/tuple_codec.h"
#include "executor/seq_scan_cursor.h"
#include "storage/bplustree.h"
#include "storage/bufferpool_manager.h"
#include "storage/storage_manager.h"
#include "storage/catalog_manager.h"
#include "transaction/transaction.h"
#include <stdexcept>

// EmptyExecutor: next() always returns nullopt (no implementation needed beyond header)

SeqScanExecutor::SeqScanExecutor(Storage& s, const std::string& table)
    : storage(&s), table_name(table), cursor_idx(0) {
    this->table = &storage->get_table(table_name);
}

SeqScanExecutor::SeqScanExecutor(BufferPoolManager* bp, StorageManager* sm, const std::string& table_name, uint32_t table_id, CreateTableStmt schema, ScanPosition initial_position)
    : bp_(bp), sm_(sm), scan_table_name_(table_name), table_id_(table_id), schema_(std::move(schema)), position_(initial_position) {}

std::optional<Tuple> SeqScanExecutor::next() {
    if (bp_) {
        auto result = next_row_from(*bp_, *sm_, table_id_, position_.page_id, position_.slot_index);
        if (!result.has_value()) return std::nullopt;
        position_ = result->second;
        return deserialize_tuple(*schema_, result->first.data(), result->first.size());
    }
    if (cursor_idx >= table->size()) return std::nullopt;
    return (*table)[cursor_idx++];
}

IndexScanExecutor::IndexScanExecutor(BPlusTree* tree, CreateTableStmt schema, ScanPosition initial_position)
    : tree_(tree), schema_(std::move(schema)), position_(initial_position) {}

std::optional<Tuple> IndexScanExecutor::next() {
    if (!tree_) return std::nullopt;
    auto result = tree_->next_entry_from(position_.page_id, position_.slot_index);
    if (!result.has_value()) return std::nullopt;
    position_ = result->second;
    return deserialize_tuple(*schema_, result->first.second.data(), result->first.second.size());
}

ValuesExecutor::ValuesExecutor(const std::vector<Expr*>& values) : values_(values) {}

std::optional<Tuple> ValuesExecutor::next() {
    if (done_) return std::nullopt;
    done_ = true;
    Tuple tuple;
    tuple.reserve(values_.size());
    for (Expr* expr : values_) {
        Value v = evaluate_expr(expr, {}, {});
        tuple.push_back(v);
    }
    return tuple;
}

InsertExecutor::InsertExecutor(std::unique_ptr<Executor> child, BPlusTree* tree,
                               CreateTableStmt schema, std::vector<std::string> insert_columns, Transaction* txn)
    : child_(std::move(child)), tree_(tree), schema_(std::move(schema)), insert_columns_(std::move(insert_columns)), txn_(txn) {
    for (size_t i = 0; i < schema_.columns.size(); ++i) {
        if (schema_.columns[i].is_primary_key) {
            pk_col_index_ = static_cast<int>(i);
            break;
        }
    }
    schema_to_child_index_.resize(schema_.columns.size(), -1);
    schema_to_ai_index_.resize(schema_.columns.size(), -1);
    if (insert_columns_.empty()) {
        for (size_t i = 0; i < schema_.columns.size(); ++i) {
            schema_to_child_index_[i] = static_cast<int>(i);
        }
    } else {
        int ai_next = 0;
        for (size_t i = 0; i < schema_.columns.size(); ++i) {
            const std::string& col_name = schema_.columns[i].name;
            int pos = -1;
            for (size_t j = 0; j < insert_columns_.size(); ++j) {
                if (insert_columns_[j] == col_name) {
                    pos = static_cast<int>(j);
                    break;
                }
            }
            if (pos >= 0) {
                schema_to_child_index_[i] = pos;
            } else if (schema_.columns[i].is_auto_increment) {
                schema_to_child_index_[i] = -1;
                schema_to_ai_index_[i] = ai_next++;
            } else {
                throw std::runtime_error("INSERT: missing value for required column '" + col_name + "'");
            }
        }
    }
}

std::optional<Tuple> InsertExecutor::next() {
    if (!tree_ || !txn_) return std::nullopt;
    if (inserted_) return std::nullopt;
    inserted_ = true;
    int count = 0;
    const std::string& db_path = tree_->get_db_path();
    const std::string& table_name = tree_->get_table_name();
    CatalogManager& catalog = tree_->get_catalog();
    while (true) {
        std::optional<Tuple> child_tuple = child_->next();
        if (!child_tuple.has_value()) break;
        
        // Build full tuple in schema order (fill omitted AUTO_INCREMENT from counters)
        Tuple full_tuple;
        full_tuple.reserve(schema_.columns.size());
        for (size_t i = 0; i < schema_.columns.size(); ++i) {
            int child_idx = schema_to_child_index_[i];
            int ai_idx = schema_to_ai_index_[i];
            if (child_idx >= 0) {
                if (static_cast<size_t>(child_idx) >= child_tuple->size()) {
                    throw std::runtime_error("INSERT: value count mismatch");
                }
                full_tuple.push_back((*child_tuple)[static_cast<size_t>(child_idx)]);
            } else if (ai_idx >= 0) {
                uint64_t val = catalog.get_and_increment_auto_increment(db_path, table_name, static_cast<uint16_t>(ai_idx));
                const std::string& dt = schema_.columns[i].data_type;
                if (dt == "INT" || dt == "INTEGER") {
                    full_tuple.push_back(Value(static_cast<int>(val)));
                } else {
                    full_tuple.push_back(Value(std::to_string(val)));
                }
            } else {
                throw std::runtime_error("INSERT: missing value for column");
            }
        }
        
        // ALWAYS generate row_id
        uint64_t row_id = catalog.get_and_increment_row_id(db_path, table_name);
        
        // Serialize tuple (user columns only)
        std::vector<uint8_t> row_bytes = serialize_tuple(schema_, full_tuple);
        
        // Prepend row_id to row_bytes (always stored)
        std::vector<uint8_t> row_with_rowid = prepend_row_id(row_id, row_bytes);
        
        std::vector<uint8_t> key_bytes;
        if (pk_col_index_ >= 0) {
            // Has explicit PK: use PK as key, check uniqueness
            const Value& pk_val = full_tuple[static_cast<size_t>(pk_col_index_)];
            const std::string& pk_type = schema_.columns[static_cast<size_t>(pk_col_index_)].data_type;
            key_bytes = serialize_pk_value(pk_val, pk_type);
            
            // Enforce primary key uniqueness
            if (tree_->key_exists(key_bytes.data(), static_cast<uint16_t>(key_bytes.size()))) {
                throw std::runtime_error("Duplicate entry for key 'PRIMARY'");
            }
        } else {
            // No PK: use row_id as key, no uniqueness check needed
            key_bytes = serialize_row_id(row_id);
        }
        
        tree_->insert(key_bytes.data(), static_cast<uint16_t>(key_bytes.size()),
                     row_with_rowid.data(), static_cast<uint16_t>(row_with_rowid.size()), *txn_);
        count++;
    }
    return Tuple{Value(count)};
}

CollectExecutor::CollectExecutor(std::unique_ptr<Executor> child) : child_(std::move(child)) {}

std::optional<Tuple> CollectExecutor::next() {
    if (!drained_) {
        while (true) {
            std::optional<Tuple> t = child_->next();
            if (!t.has_value()) break;
            rows_.push_back(std::move(*t));
        }
        drained_ = true;
    }
    if (index_ >= rows_.size()) return std::nullopt;
    return std::move(rows_[index_++]);
}

DeleteExecutor::DeleteExecutor(std::unique_ptr<Executor> child, class BPlusTree* tree, CreateTableStmt schema)
    : child_(std::move(child)), tree_(tree), schema_(std::move(schema)) {
    for (size_t i = 0; i < schema_.columns.size(); ++i) {
        if (schema_.columns[i].is_primary_key) {
            pk_col_index_ = static_cast<int>(i);
            break;
        }
    }
}

std::optional<Tuple> DeleteExecutor::next() {
    if (!tree_ || done_) return std::nullopt;
    done_ = true;
    int count = 0;
    while (true) {
        std::optional<Tuple> tuple = child_->next();
        if (!tuple.has_value()) break;
        
        std::vector<uint8_t> key_bytes;
        if (pk_col_index_ >= 0) {
            // Has explicit PK: delete by PK value
            if (static_cast<size_t>(pk_col_index_) >= tuple->size()) continue;
            const Value& pk_val = (*tuple)[static_cast<size_t>(pk_col_index_)];
            const std::string& pk_type = schema_.columns[static_cast<size_t>(pk_col_index_)].data_type;
            key_bytes = serialize_pk_value(pk_val, pk_type);
        } else {
            // No PK: delete by row_id
            // The tuple comes from SeqScan which deserializes from B+ tree row_bytes
            // Need to extract row_id - but current deserialize_tuple doesn't include row_id
            // We need to get row_id from the raw leaf entry
            // For now, throw error - DELETE without PK requires SeqScan to expose row_id
            throw std::runtime_error("DELETE from table without PRIMARY KEY not yet supported (row_id exposure needed)");
        }
        
        count += tree_->remove(key_bytes.data(), static_cast<uint16_t>(key_bytes.size()));
    }
    return Tuple{Value(count)};
}

UpdateExecutor::UpdateExecutor(std::unique_ptr<Executor> child, BPlusTree* tree,
                               CreateTableStmt schema, std::vector<Assignment> assignments,
                               Transaction* txn)
    : child_(std::move(child)), tree_(tree), schema_(std::move(schema)),
      assignments_(assignments), txn_(txn) {
    // Build column_names from schema
    for (const auto& col : schema_.columns) {
        column_names_.push_back(col.name);
    }
    // Find PK column index
    for (size_t i = 0; i < schema_.columns.size(); ++i) {
        if (schema_.columns[i].is_primary_key) {
            pk_col_index_ = static_cast<int>(i);
            break;
        }
    }
}

std::optional<Tuple> UpdateExecutor::next() {
    if (!tree_ || !txn_ || done_) return std::nullopt;
    done_ = true;
    int count = 0;

    while (true) {
        std::optional<Tuple> old_tuple = child_->next();
        if (!old_tuple.has_value()) break;

        // Step 1: Determine old key and row_id (if no PK)
        std::vector<uint8_t> old_key_bytes;
        uint64_t row_id = 0;  // Only used if no PK

        if (pk_col_index_ >= 0) {
            // Has explicit PK: use PK as key
            if (static_cast<size_t>(pk_col_index_) >= old_tuple->size()) continue;
            const Value& old_pk_val = (*old_tuple)[static_cast<size_t>(pk_col_index_)];
            const std::string& pk_type = schema_.columns[static_cast<size_t>(pk_col_index_)].data_type;
            old_key_bytes = serialize_pk_value(old_pk_val, pk_type);
        } else {
            // No PK: For now, throw error (row_id exposure not yet implemented)
            throw std::runtime_error("UPDATE from table without PRIMARY KEY not yet supported (row_id exposure needed)");
        }

        // Step 2: Apply assignments to create new tuple
        Tuple new_tuple = *old_tuple;  // Copy old tuple
        for (const auto& assign : assignments_) {
            // Find column index
            int col_idx = -1;
            for (size_t i = 0; i < column_names_.size(); ++i) {
                if (column_names_[i] == assign.column) {
                    col_idx = static_cast<int>(i);
                    break;
                }
            }
            if (col_idx < 0 || static_cast<size_t>(col_idx) >= new_tuple.size()) continue;

            // Evaluate assignment expression using OLD tuple values
            Value new_value = evaluate_expr(assign.value, *old_tuple, column_names_);
            new_tuple[col_idx] = new_value;
        }

        // Step 3: Serialize new tuple and prepend row_id
        std::vector<uint8_t> new_row_bytes = serialize_tuple(schema_, new_tuple);
        std::vector<uint8_t> new_row_with_rowid;

        if (pk_col_index_ >= 0) {
            // Has PK: generate new row_id, prepend
            uint64_t new_row_id = tree_->get_catalog().get_and_increment_row_id(
                tree_->get_db_path(), tree_->get_table_name());
            new_row_with_rowid = prepend_row_id(new_row_id, new_row_bytes);
        } else {
            // No PK: reuse same row_id
            new_row_with_rowid = prepend_row_id(row_id, new_row_bytes);
        }

        // Step 4: Determine new key (PK might have changed)
        std::vector<uint8_t> new_key_bytes;
        if (pk_col_index_ >= 0) {
            const Value& new_pk_val = new_tuple[static_cast<size_t>(pk_col_index_)];
            const std::string& pk_type = schema_.columns[static_cast<size_t>(pk_col_index_)].data_type;
            new_key_bytes = serialize_pk_value(new_pk_val, pk_type);

            // Check uniqueness if PK changed
            if (new_key_bytes != old_key_bytes) {
                if (tree_->key_exists(new_key_bytes.data(), static_cast<uint16_t>(new_key_bytes.size()))) {
                    throw std::runtime_error("Duplicate entry for key 'PRIMARY' (UPDATE would create duplicate)");
                }
            }
        } else {
            new_key_bytes = serialize_row_id(row_id);
        }

        // Step 5: Delete old row
        tree_->remove(old_key_bytes.data(), static_cast<uint16_t>(old_key_bytes.size()));

        // Step 6: Insert new row
        tree_->insert(new_key_bytes.data(), static_cast<uint16_t>(new_key_bytes.size()),
                     new_row_with_rowid.data(), static_cast<uint16_t>(new_row_with_rowid.size()), *txn_);

        count++;
    }

    return Tuple{Value(count)};
}

// FilterExecutor implementation
FilterExecutor::FilterExecutor(std::unique_ptr<Executor> child, Expr* pred, 
                               const std::vector<std::string>& cols)
    : child(std::move(child)), predicate(pred), column_names(cols) {
}

std::optional<Tuple> FilterExecutor::next() {
    while (true) {
        std::optional<Tuple> tuple = child->next();
        if (!tuple.has_value()) {
            return std::nullopt;  // No more tuples from child
        }
        
        // Evaluate predicate on this tuple
        if (evaluate_predicate(predicate, tuple.value(), column_names)) {
            return tuple;  // Tuple passes filter
        }
        // Otherwise, continue loop to get next tuple
    }
}

// ProjectExecutor implementation
ProjectExecutor::ProjectExecutor(std::unique_ptr<Executor> child,
                                  const std::vector<Expr*>& proj,
                                  const std::vector<std::string>& cols,
                                  bool own_projections)
    : child(std::move(child)), projections(proj), column_names(cols) {
    if (own_projections) {
        owned_projections_ = proj;
    }
}

ProjectExecutor::~ProjectExecutor() {
    for (Expr* e : owned_projections_) {
        delete e;
    }
}

std::optional<Tuple> ProjectExecutor::next() {
    std::optional<Tuple> input_tuple = child->next();
    if (!input_tuple.has_value()) {
        return std::nullopt;  // No more tuples from child
    }
    
    // Evaluate each projection expression
    Tuple output_tuple;
    for (Expr* proj : projections) {
        Value value = evaluate_expr(proj, input_tuple.value(), column_names);
        output_tuple.push_back(value);
    }
    
    return output_tuple;
}
