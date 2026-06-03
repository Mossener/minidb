// 事务模块实现 - Begin/Commit/Abort 及 MVCC 可见性判断

#include "transaction/transaction.h"

namespace minidb {

// 事务构造函数: 设置事务ID、读时间戳(快照点)，初始状态为 RUNNING
Transaction::Transaction(txn_id_t txn_id, ts_t read_ts)
    : txn_id_(txn_id), read_ts_(read_ts), state_(TransactionState::RUNNING) {}

TransactionManager::TransactionManager() {}

// 析构函数: 释放所有活跃事务对象 (通常在数据库关闭时调用)
TransactionManager::~TransactionManager() {
    for (auto &[_, txn] : transactions_) {
        delete txn;
    }
}

// Begin: 开始一个新事务
// 1. 原子分配事务ID和读时间戳 (read_ts 即快照点)
// 2. 创建 Transaction 对象并注册到活跃事务表
// 返回: 新创建的事务对象指针
Transaction *TransactionManager::Begin() {
    txn_id_t txn_id = next_txn_id_++;
    ts_t read_ts = next_ts_++;
    auto *txn = new Transaction(txn_id, read_ts);
    std::lock_guard<std::mutex> lock(mutex_);
    transactions_[txn_id] = txn;
    return txn;
}

// Commit: 提交事务
// 1. 分配提交时间戳 commit_ts (写入 begin_ts/end_ts 时使用)
// 2. 将事务状态设为 COMMITTED
// 注意: 实际的元组时间戳更新由上层 MiniDB::CommitTxn 完成
bool TransactionManager::Commit(Transaction *txn) {
    ts_t commit_ts = next_ts_++;
    txn->SetCommitTs(commit_ts);
    txn->SetState(TransactionState::COMMITTED);
    return true;
}

// Abort: 中止事务
// 将事务状态设为 ABORTED，后续由 MiniDB::AbortTxn 进行物理回滚
bool TransactionManager::Abort(Transaction *txn) {
    txn->SetState(TransactionState::ABORTED);
    return true;
}

// 根据事务ID在活跃事务表中查找事务对象
// 返回: 找到返回事务指针，未找到返回 nullptr
Transaction *TransactionManager::GetTransaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txn_id);
    if (it != transactions_.end()) return it->second;
    return nullptr;
}

// 判断指定事务ID是否仍然处于活跃(RUNNING)状态
// 注意: 已提交或已中止的事务视为非活跃
bool TransactionManager::IsActive(txn_id_t txn_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txn_id);
    if (it == transactions_.end()) return false;
    return it->second->GetState() == TransactionState::RUNNING;
}

// MVCC 可见性判断: 判断元组版本 (meta, begin_ts/end_ts) 是否对读事务可见
// 参数: meta - 元组元数据, read_ts - 读事务快照时间戳, reader_txn_id - 读事务ID
// 返回: true 表示可见, false 表示不可见
bool TransactionManager::IsVisible(const TupleMeta &meta, ts_t read_ts, txn_id_t reader_txn_id) const {
    // 规则1: 自己插入的未提交元组 (且尚未被自己删除) → 可见 (支持读己写)
    if (meta.begin_ts == TS_UNCOMMITTED && meta.txn_id == reader_txn_id) {
        return meta.end_ts == TS_MAX;
    }
    // 规则2: 其他事务插入的未提交元组 → 不可见 (隔离性)
    if (meta.begin_ts == TS_UNCOMMITTED) return false;
    // 规则3: 版本创建于快照之后 → 不可见 (快照隔离)
    if (meta.begin_ts > read_ts) return false;
    // 规则4: 版本在快照时刻已被删除 → 不可见 (end_ts <= read_ts 表示在快照前就删除了)
    if (meta.end_ts != TS_MAX && meta.end_ts <= read_ts) return false;
    // 规则5: 创建该版本的事务仍在运行中 → 不可见 (还未提交)
    if (IsActive(meta.txn_id)) return false;
    return true;
}

} // namespace minidb
