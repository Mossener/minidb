// 全局配置模块 - 系统常量、类型别名和参数定义

#pragma once

#include <cstdint>
#include <chrono>

namespace minidb {

// ── 存储相关常量 ─────────────────────────────────
static constexpr int PAGE_SIZE = 4096;           // 页面大小 4KB
static constexpr int BUFFER_POOL_SIZE = 128;     // 缓冲池可缓存的页面数
static constexpr int MAX_DATABASES = 64;         // 最大数据库数量
static constexpr int MAX_TABLES_PER_DB = 256;    // 每个数据库最大表数
static constexpr int BPLUS_TREE_DEGREE = 64;     // B+树阶数 (扇出度)
static constexpr int MAX_COLUMNS = 256;          // 每表最大列数
static constexpr int MAX_TUPLE_SIZE = PAGE_SIZE - 128;  // 单个元组最大字节数

// ── 类型别名 ─────────────────────────────────────
using page_id_t = int32_t;     // 页面唯一标识
using frame_id_t = int32_t;    // 缓冲池帧索引
using table_id_t = int32_t;    // 表唯一标识
using column_id_t = int16_t;   // 列唯一标识
using rid_t = int64_t;         // 元组ID (高32位=page_id, 低32位=页内偏移)
using txn_id_t = int64_t;      // 事务唯一标识

// ── 无效值常量 ───────────────────────────────────
static constexpr page_id_t INVALID_PAGE_ID = -1;   // 无效页面ID
static constexpr frame_id_t INVALID_FRAME_ID = -1; // 无效帧索引
static constexpr txn_id_t INVALID_TXN_ID = -1;     // 无效事务ID

} // namespace minidb
