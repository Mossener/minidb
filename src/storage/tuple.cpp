// 元组(Tuple)实现 - 行数据的序列化、反序列化及工具方法

#include "storage/tuple.h"
#include <cstring>

namespace minidb {

// 构造函数: 从原始字节数据和 Schema 构造元组
Tuple::Tuple(std::vector<char> data, const Schema &schema)
    : data_(std::move(data)) {}

// SerializeTo: 将元组的列数据拷贝到目标缓冲区
// dest 指向页面中放置原始数据的位置 (即元组头部之后)
void Tuple::SerializeTo(char *dest) const {
    memcpy(dest, data_.data(), data_.size());
}

// DeserializeFrom: 从原始字节流反序列化为 Tuple 对象
// src: 指向页面中列数据起始位置的指针
// 使用 Schema 的序列化大小确定要拷贝的字节数
Tuple Tuple::DeserializeFrom(const char *src, const Schema &schema) {
    std::vector<char> data(src, src + schema.GetSerializedSize());
    return Tuple(std::move(data), schema);
}

// MakeNumericTuple: 创建一个只包含单列整数数据的测试用元组
// val 被写入 data 的前4字节 (对应第一个 INTEGER 列)
Tuple Tuple::MakeNumericTuple(const Schema &schema, int val) {
    std::vector<char> data(schema.GetSerializedSize(), 0);
    memcpy(data.data(), &val, sizeof(val));
    return Tuple(std::move(data), schema);
}

// SetValueAt: 在元组数据的指定偏移处写入值 (用于就地更新)
// 边界检查: 确保写入不会越界
void Tuple::SetValueAt(size_t offset, const char *val, size_t len) {
    if (offset + len <= data_.size()) {
        memcpy(data_.data() + offset, val, len);
    }
}

} // namespace minidb
