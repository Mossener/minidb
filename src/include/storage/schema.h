// 模式(Schema)模块 - 定义表结构的列类型和序列化布局
// Column 描述单列属性; Schema 管理整张表的列集合、计算各列偏移量和总序列化大小

#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include "common/config.h"

namespace minidb {

// 列数据类型枚举: 整型 / 浮点 / 变长字符串 / 布尔
enum class TypeId { INTEGER, FLOAT, VARCHAR, BOOLEAN };

// 列定义: 包含列名、类型、长度(仅 VARCHAR 有效)、是否可空
struct Column {
    std::string name;   // 列名
    TypeId type;         // 列数据类型
    size_t length;        // 存储长度 (仅 VARCHAR 有意义，其他类型自动计算)
    bool nullable;        // 是否允许 NULL

    Column(std::string name, TypeId type, size_t length = 0, bool nullable = true)
        : name(std::move(name)), type(type), length(length), nullable(nullable) {}

    // 返回该列在行数据中占用的字节数
    size_t GetStorageSize() const {
        switch (type) {
            case TypeId::INTEGER: return 4;   // 32位有符号整数
            case TypeId::FLOAT: return 8;      // 64位双精度浮点
            case TypeId::BOOLEAN: return 1;    // 1字节布尔值
            case TypeId::VARCHAR: return length; // 变长字符串，取设定长度
        }
        return 0;
    }
};

// 模式: 描述一张表的列结构和序列化布局
class Schema {
public:
    // 构造函数: 接收列定义列表，自动计算各列偏移量和总大小
    explicit Schema(std::vector<Column> columns);

    const Column &GetColumn(size_t idx) const { return columns_[idx]; }
    size_t GetColumnCount() const { return columns_.size(); }
    // 返回整行序列化后的字节数
    size_t GetSerializedSize() const;
    // 返回指定列在行数据中的字节偏移量
    size_t GetColumnOffset(size_t idx) const;

    // 工厂方法: 创建默认的示例表结构 (id INT, name VARCHAR(64), salary FLOAT)
    static std::vector<Column> MakeDefaultSchema();

private:
    std::vector<Column> columns_;     // 列定义列表
    std::vector<size_t> offsets_;     // 每列在行数据中的偏移量 (字节)
    size_t serialized_size_ = 0;      // 一整行序列化后的总字节数
};

} // namespace minidb
