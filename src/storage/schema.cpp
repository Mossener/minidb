#include "storage/schema.h"

namespace minidb {

Schema::Schema(std::vector<Column> columns) : columns_(std::move(columns)) {
    size_t offset = 0;
    for (const auto &col : columns_) {
        offsets_.push_back(offset);
        offset += col.GetStorageSize();
    }
    serialized_size_ = offset;
}

size_t Schema::GetSerializedSize() const {
    return serialized_size_;
}

size_t Schema::GetColumnOffset(size_t idx) const {
    return offsets_[idx];
}

std::vector<Column> Schema::MakeDefaultSchema() {
    return {
        Column("id", TypeId::INTEGER),
        Column("name", TypeId::VARCHAR, 64),
        Column("salary", TypeId::FLOAT)
    };
}

} // namespace minidb
