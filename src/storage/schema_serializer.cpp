#include "storage/schema_serializer.h"
#include "parser/statements/create.h"
#include <cstring>
#include <stdexcept>

std::vector<uint8_t> serialize_schema(const CreateTableStmt& schema) {
    std::vector<uint8_t> result;
    
    // Reserve space (estimate: 100 bytes per column)
    result.reserve(100 * schema.columns.size() + schema.table_name.size() + 20);
    
    // Write number of columns (2 bytes)
    uint16_t num_cols = static_cast<uint16_t>(schema.columns.size());
    result.push_back(static_cast<uint8_t>(num_cols & 0xFF));
    result.push_back(static_cast<uint8_t>((num_cols >> 8) & 0xFF));
    
    // Write table name length (2 bytes)
    uint16_t name_len = static_cast<uint16_t>(schema.table_name.size());
    result.push_back(static_cast<uint8_t>(name_len & 0xFF));
    result.push_back(static_cast<uint8_t>((name_len >> 8) & 0xFF));
    
    // Write table name
    result.insert(result.end(), schema.table_name.begin(), schema.table_name.end());
    
    // Write each column
    for (const auto& col : schema.columns) {
        // Column name length (2 bytes)
        uint16_t col_name_len = static_cast<uint16_t>(col.name.size());
        result.push_back(static_cast<uint8_t>(col_name_len & 0xFF));
        result.push_back(static_cast<uint8_t>((col_name_len >> 8) & 0xFF));
        
        // Column name
        result.insert(result.end(), col.name.begin(), col.name.end());
        
        // Data type length (2 bytes)
        uint16_t type_len = static_cast<uint16_t>(col.data_type.size());
        result.push_back(static_cast<uint8_t>(type_len & 0xFF));
        result.push_back(static_cast<uint8_t>((type_len >> 8) & 0xFF));
        
        // Data type
        result.insert(result.end(), col.data_type.begin(), col.data_type.end());
        
        // Column flags (1 byte)
        uint8_t flags = 0;
        if (col.is_primary_key) flags |= COL_FLAG_PRIMARY_KEY;
        if (col.is_unique) flags |= COL_FLAG_UNIQUE;
        if (col.is_not_null) flags |= COL_FLAG_NOT_NULL;
        if (col.is_auto_increment) flags |= COL_FLAG_AUTO_INCREMENT;
        result.push_back(flags);
    }
    
    return result;
}

CreateTableStmt deserialize_schema(const uint8_t* data, uint16_t size) {
    if (size < 4) {
        throw std::runtime_error("Schema data too small");
    }
    
    CreateTableStmt schema;
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    
    // Read number of columns
    uint16_t num_cols = static_cast<uint16_t>(ptr[0]) | 
                       (static_cast<uint16_t>(ptr[1]) << 8);
    ptr += 2;
    
    // Read table name length
    if (ptr + 2 > end) throw std::runtime_error("Invalid schema: table name length");
    uint16_t name_len = static_cast<uint16_t>(ptr[0]) | 
                       (static_cast<uint16_t>(ptr[1]) << 8);
    ptr += 2;
    
    // Read table name
    if (ptr + name_len > end) throw std::runtime_error("Invalid schema: table name");
    schema.table_name.assign(reinterpret_cast<const char*>(ptr), name_len);
    ptr += name_len;
    
    // Read columns
    schema.columns.reserve(num_cols);
    for (uint16_t i = 0; i < num_cols; ++i) {
        ColumnDef col;
        
        // Read column name length
        if (ptr + 2 > end) throw std::runtime_error("Invalid schema: column name length");
        uint16_t col_name_len = static_cast<uint16_t>(ptr[0]) | 
                               (static_cast<uint16_t>(ptr[1]) << 8);
        ptr += 2;
        
        // Read column name
        if (ptr + col_name_len > end) throw std::runtime_error("Invalid schema: column name");
        col.name.assign(reinterpret_cast<const char*>(ptr), col_name_len);
        ptr += col_name_len;
        
        // Read data type length
        if (ptr + 2 > end) throw std::runtime_error("Invalid schema: type length");
        uint16_t type_len = static_cast<uint16_t>(ptr[0]) | 
                           (static_cast<uint16_t>(ptr[1]) << 8);
        ptr += 2;
        
        // Read data type
        if (ptr + type_len > end) throw std::runtime_error("Invalid schema: type");
        col.data_type.assign(reinterpret_cast<const char*>(ptr), type_len);
        ptr += type_len;
        
        // Read flags
        if (ptr >= end) throw std::runtime_error("Invalid schema: flags");
        uint8_t flags = *ptr++;
        col.is_primary_key = (flags & COL_FLAG_PRIMARY_KEY) != 0;
        col.is_unique = (flags & COL_FLAG_UNIQUE) != 0;
        col.is_not_null = (flags & COL_FLAG_NOT_NULL) != 0;
        col.is_auto_increment = (flags & COL_FLAG_AUTO_INCREMENT) != 0;

        schema.columns.push_back(col);
    }
    
    if (ptr != end) {
        throw std::runtime_error("Schema data has extra bytes");
    }
    
    return schema;
}
