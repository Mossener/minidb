// 数据库主模块 - MiniDB 核心类，统筹管理存储、索引、事务和 SQL 执行
// 持有 DiskManager、BufferPoolManager、TransactionManager 以及所有表的注册信息

#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/table_heap.h"
#include "storage/schema.h"
#include "storage/tuple_meta.h"
#include "index/b_plus_tree.h"
#include "transaction/transaction.h"
#include "parser/parser.h"
#include "storage/log_manager.h"
#include "common/lock_manager.h"

namespace minidb {

// 表信息: 聚合一张表的存储、模式和索引组件
struct TableInfo {
    std::string name;       // 表名
    TableHeap *heap;        // 表堆 (管理元组的物理存储)
    Schema *schema;         // 表结构 (列定义和布局)
    BPlusTree *index;       // B+树索引 (可选)
    bool has_index;         // 是否已创建索引

    TableInfo() : heap(nullptr), schema(nullptr), index(nullptr), has_index(false) {}
    ~TableInfo() {
        delete heap;
        delete schema;
        delete index;
    }
};

// MiniDB: 数据库系统的顶层协调类
class MiniDB {
public:
    MiniDB();
    ~MiniDB();

    // DDL 操作: 创建/删除表，创建索引
    bool CreateTable(const std::string &name, const Schema &schema);
    bool DropTable(const std::string &name);
    bool CreateIndex(const std::string &table_name);

    // 底层存储操作 (接受预构造的 Tuple)
    bool Insert(const std::string &table_name, const Tuple &tuple, Transaction *txn);
    bool Delete(const std::string &table_name, int64_t key, Transaction *txn);
    std::vector<Tuple> Scan(const std::string &table_name, Transaction *txn);

    // SQL 层面的操作 (解析 AST 后执行)
    bool ExecCreateTable(const SQLStatement &stmt);
    bool ExecInsert(const SQLStatement &stmt, Transaction *txn);
    std::vector<Tuple> ExecSelect(const SQLStatement &stmt, Transaction *txn);
    int ExecDelete(const SQLStatement &stmt, Transaction *txn);

    // 查找指定名称的表信息
    TableInfo *GetTable(const std::string &name);
    TransactionManager *GetTxnMgr() { return &txn_mgr_; }

    // 事务管理 (对外暴露的事务接口)
    Transaction *BeginTxn();
    bool CommitTxn(Transaction *txn);
    bool AbortTxn(Transaction *txn);

private:
    // 从 AST 的字面值列表构造 Tuple 对象 (类型转换 + 序列化)
    Tuple BuildTuple(const std::vector<LiteralValue> &values, const Schema &schema);
    // 评估 WHERE 条件: 检查元组是否满足比较条件
    bool EvalCondition(const Tuple &tuple, const Schema &schema,
                       const CompareCondition &cond);

    // 崩溃恢复: 启动时从 WAL 重放已提交事务的操作
    void DoRecover();

    DiskManager disk_manager_;                          // 磁盘管理器
    BufferPoolManager bpm_;                             // 缓冲池管理器
    TransactionManager txn_mgr_;                        // 事务管理器
    LogManager log_mgr_;                                // WAL 日志管理器
    LockManager lock_mgr_;                              // 行级锁管理器
    std::unordered_map<std::string, TableInfo *> tables_; // 表注册表
};

} // namespace minidb
