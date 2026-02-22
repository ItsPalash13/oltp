#include "executor/tuple_codec.h"
#include "parser/statements/create.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>

static bool is_int_type(const std::string& data_type) {
    std::string t = data_type;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    return t.find("int") != std::string::npos;
}

std::vector<uint8_t> serialize_tuple(const CreateTableStmt& schema, const Tuple& tuple) {
    if (tuple.size() != schema.columns.size()) {
        throw std::runtime_error("Tuple size does not match schema");
    }
    std::vector<uint8_t> out;
    for (size_t i = 0; i < schema.columns.size(); ++i) {
        const auto& col = schema.columns[i];
        const Value& v = tuple[i];
        if (is_int_type(col.data_type)) {
            int val = std::get<int>(v);
            uint8_t buf[4];
            std::memcpy(buf, &val, 4);
            out.insert(out.end(), buf, buf + 4);
        } else {
            const std::string& s = std::get<std::string>(v);
            uint16_t len = static_cast<uint16_t>(s.size());
            out.push_back(static_cast<uint8_t>(len & 0xFF));
            out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            out.insert(out.end(), s.begin(), s.end());
        }
    }
    return out;
}

std::optional<Tuple> deserialize_tuple(const CreateTableStmt& schema, const uint8_t* data, size_t size) {
    Tuple tuple;
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    for (const auto& col : schema.columns) {
        if (is_int_type(col.data_type)) {
            if (ptr + 4 > end) return std::nullopt;
            int val;
            std::memcpy(&val, ptr, 4);
            ptr += 4;
            tuple.push_back(val);
        } else {
            if (ptr + 2 > end) return std::nullopt;
            uint16_t len = static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
            ptr += 2;
            if (ptr + len > end) return std::nullopt;
            tuple.push_back(std::string(reinterpret_cast<const char*>(ptr), len));
            ptr += len;
        }
    }
    return tuple;
}

std::vector<uint8_t> serialize_pk_value(const Value& v, const std::string& data_type) {
    std::vector<uint8_t> out;
    if (is_int_type(data_type)) {
        int val = std::get<int>(v);
        uint8_t buf[4];
        std::memcpy(buf, &val, 4);
        out.insert(out.end(), buf, buf + 4);
    } else {
        const std::string& s = std::get<std::string>(v);
        uint16_t len = static_cast<uint16_t>(s.size());
        out.push_back(static_cast<uint8_t>(len & 0xFF));
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

std::vector<uint8_t> serialize_row_id(uint64_t row_id) {
    std::vector<uint8_t> out(8);
    std::memcpy(out.data(), &row_id, 8);
    return out;
}

std::vector<uint8_t> prepend_row_id(uint64_t row_id, const std::vector<uint8_t>& row_bytes) {
    std::vector<uint8_t> out(8);
    std::memcpy(out.data(), &row_id, 8);
    out.insert(out.end(), row_bytes.begin(), row_bytes.end());
    return out;
}

uint64_t extract_row_id(const uint8_t* row_bytes, size_t size) {
    if (size < 8) throw std::runtime_error("Row bytes too small to contain row_id");
    uint64_t row_id;
    std::memcpy(&row_id, row_bytes, 8);
    return row_id;
}

std::vector<uint8_t> strip_row_id(const uint8_t* row_bytes, size_t size) {
    if (size < 8) throw std::runtime_error("Row bytes too small to contain row_id");
    return std::vector<uint8_t>(row_bytes + 8, row_bytes + size);
}
