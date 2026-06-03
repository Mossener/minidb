// 元组元数据模块 - 定义元组在页面上的存储格式和读写接口
// 每个元组在页面上的布局: [数据长度(4B)] [txn_id(8B)] [begin_ts(8B)] [end_ts(8B)] [实际数据(变长)]

#pragma once

#include <cstring>
#include "common/config.h"

namespace minidb {

using ts_t = int64_t;

// 时间戳常量: TS_UNCOMMITTED 表示未提交 (begin_ts 用于插入, end_ts 用于删除标记)
// TS_MAX 表示尚未被删除 (end_ts 的默认值)
static constexpr ts_t TS_MAX = INT64_MAX;
static constexpr ts_t TS_UNCOMMITTED = 0;

// 元组元数据: 记录元组的创建者、生存期，供 MVCC 可见性判断使用
struct TupleMeta {
    txn_id_t txn_id;  // 创建/修改该版本的事务ID
    ts_t begin_ts;     // 版本开始时间 (插入时间戳)
    ts_t end_ts;       // 版本结束时间 (删除时间戳, TS_MAX 表示尚未删除)
};

// 元组在页面上的存储布局 (Storage layout per tuple):
// [raw_data_size : int32_t]         | 4 bytes  — 实际数据大小
// [txn_id        : int64_t]         | 8 bytes  — 事务ID
// [begin_ts      : int64_t]         | 8 bytes  — 版本开始时间戳
// [end_ts        : int64_t]         | 8 bytes  — 版本结束时间戳
// [raw_data      : raw_data_size]   | variable — 实际列数据

// 元数据头部大小 = 4字节数据长度 + 24字节 TupleMeta
static constexpr int TUPLE_META_HEADER_SIZE = sizeof(int32_t) + sizeof(TupleMeta);

// 从页面数据中读取指定偏移量处的 TupleMeta
inline TupleMeta ReadTupleMeta(const char *page_data, int offset) {
    TupleMeta meta;
    // 跳过 raw_data_size 字段，定位到 TupleMeta 起始位置
    const char *p = page_data + offset + sizeof(int32_t);
    memcpy(&meta.txn_id, p, sizeof(int64_t));
    memcpy(&meta.begin_ts, p + sizeof(int64_t), sizeof(int64_t));
    memcpy(&meta.end_ts, p + 2 * sizeof(int64_t), sizeof(int64_t));
    return meta;
}

// 将 TupleMeta 写入页面数据的指定偏移量处
inline void WriteTupleMeta(char *page_data, int offset, const TupleMeta &meta) {
    char *p = page_data + offset + sizeof(int32_t);
    memcpy(p, &meta.txn_id, sizeof(int64_t));
    memcpy(p + sizeof(int64_t), &meta.begin_ts, sizeof(int64_t));
    memcpy(p + 2 * sizeof(int64_t), &meta.end_ts, sizeof(int64_t));
}

// 读取元组原始数据的大小 (存储在头部前4字节)
inline int GetRawDataSize(const char *page_data, int offset) {
    int32_t s;
    memcpy(&s, page_data + offset, sizeof(int32_t));
    return s;
}

// 计算元组在页面上占用的总空间 = 头部大小 + 原始数据大小
inline int GetTupleStorageSize(const char *page_data, int offset) {
    return TUPLE_META_HEADER_SIZE + GetRawDataSize(page_data, offset);
}

// 获取实际数据部分相对于元组起始位置的偏移量 (即头部大小)
inline int GetTupleDataOffset() {
    return TUPLE_META_HEADER_SIZE;
}

} // namespace minidb
