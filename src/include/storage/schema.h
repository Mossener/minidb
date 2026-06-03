#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include "common/config.h"

namespace minidb {

enum class TypeId { INTEGER, FLOAT, VARCHAR, BOOLEAN };

struct Column {
    std::string name;
    TypeId type;
    size_t length;
    bool nullable;

    Column(std::string name, TypeId type, size_t length = 0, bool nullable = true)
        : name(std::move(name)), type(type), length(length), nullable(nullable) {}

    size_t GetStorageSize() const {
        switch (type) {
            case TypeId::INTEGER: return 4;
            case TypeId::FLOAT: return 8;
            case TypeId::BOOLEAN: return 1;
            case TypeId::VARCHAR: return length;
        }
        return 0;
    }
};

class Schema {
public:
    explicit Schema(std::vector<Column> columns);

    const Column &GetColumn(size_t idx) const { return columns_[idx]; }
    size_t GetColumnCount() const { return columns_.size(); }
    size_t GetSerializedSize() const;
    size_t GetColumnOffset(size_t idx) const;

    static std::vector<Column> MakeDefaultSchema();

private:
    std::vector<Column> columns_;
    std::vector<size_t> offsets_;
    size_t serialized_size_ = 0;
};

} // namespace minidb
