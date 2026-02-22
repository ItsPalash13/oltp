#ifndef SCHEMA_SERIALIZER_H
#define SCHEMA_SERIALIZER_H

#include "parser/statements/create.h"
#include <vector>
#include <cstdint>

// Column flags
enum ColumnFlags : uint8_t {
    COL_FLAG_PRIMARY_KEY = 1 << 0,
    COL_FLAG_UNIQUE = 1 << 1,
    COL_FLAG_NOT_NULL = 1 << 2,
    COL_FLAG_AUTO_INCREMENT = 1 << 3
};

// Serialize CreateTableStmt to binary format
// Returns byte vector containing serialized schema
std::vector<uint8_t> serialize_schema(const CreateTableStmt& schema);

// Deserialize binary format to CreateTableStmt
// Throws if data is invalid
CreateTableStmt deserialize_schema(const uint8_t* data, uint16_t size);

// Schema Binary Format:
// +-------------------+
// | num_columns (2B)  |
// | table_name_len(2B)|
// | table_name        |  variable length
// | column_1         |
// | column_2         |
// | ...              |
// +-------------------+
//
// Column Format:
// +-------------------+
// | name_len   (2B)   |
// | name              |  variable length
// | type_len   (2B)   |
// | type              |  variable length
// | flags      (1B)   |  ColumnFlags
// +-------------------+

#endif // SCHEMA_SERIALIZER_H
