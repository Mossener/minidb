// 模式(Schema)实现 - 列布局计算和默认表结构工厂

#include "storage/schema.h"

namespace minidb {

// Schema 构造函数: 根据列定义列表计算每列的偏移量和整行总大小
// 列数据在行缓冲中是紧凑排列的，偏移量由各列的存储大小累加而得
Schema::Schema(std::vector<Column> columns) : columns_(std::move(columns)) {
    size_t offset = 0;
    for (const auto &col : columns_) {
        offsets_.push_back(offset);       // 当前列的起始偏移
        offset += col.GetStorageSize();   // 累加该列占用空间
    }
    serialized_size_ = offset;            // 所有列占用总和 = 一行的大小
}

// 返回整行序列化后的总字节数 (所有列的存储大小之和)
size_t Schema::GetSerializedSize() const {
    return serialized_size_;
}

// 返回指定列索引在行数据中的字节偏移量
size_t Schema::GetColumnOffset(size_t idx) const {
    return offsets_[idx];
}

// 工厂方法: 创建默认表结构，用于测试和示例
// 包含三列: id(INT), name(VARCHAR 64), salary(FLOAT)
std::vector<Column> Schema::MakeDefaultSchema() {
    return {
        Column("id", TypeId::INTEGER),
        Column("name", TypeId::VARCHAR, 64),
        Column("salary", TypeId::FLOAT)
    };
}

} // namespace minidb
