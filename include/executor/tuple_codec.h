#ifndef TUPLE_CODEC_H
#define TUPLE_CODEC_H

#include "executor/types.h"
#include "parser/statements/create.h"
#include <vector>
#include <cstdint>

// Serialize a tuple to row bytes (schema order; INT=4B, string=2B len + bytes)
std::vector<uint8_t> serialize_tuple(const CreateTableStmt& schema, const Tuple& tuple);

// Deserialize row bytes to tuple; returns empty optional on error
std::optional<Tuple> deserialize_tuple(const CreateTableStmt& schema, const uint8_t* data, size_t size);

// Serialize primary key value to key bytes (for B+ tree insert)
std::vector<uint8_t> serialize_pk_value(const Value& v, const std::string& data_type);

// Serialize row_id (8 bytes, little-endian)
std::vector<uint8_t> serialize_row_id(uint64_t row_id);

// Prepend row_id to row_bytes
std::vector<uint8_t> prepend_row_id(uint64_t row_id, const std::vector<uint8_t>& row_bytes);

// Extract row_id from row_bytes (first 8 bytes)
uint64_t extract_row_id(const uint8_t* row_bytes, size_t size);

// Strip row_id from row_bytes (returns bytes after first 8)
std::vector<uint8_t> strip_row_id(const uint8_t* row_bytes, size_t size);

#endif // TUPLE_CODEC_H
