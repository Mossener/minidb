// 事务模块 - 基于 MVCC 的事务管理
// 提供事务对象定义、事务状态管理、时间戳分配和版本可见性判断

#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include "common/config.h"
#include "storage/tuple_meta.h"

namespace minidb {

// 事务状态: 运行中 / 已提交 / 已中止
enum class TransactionState { RUNNING, COMMITTED, ABORTED };

// 写记录: 记录事务修改过的每个元组的 RID、旧 end_ts、操作类型
// Commit: 根据类型写入 begin_ts / end_ts
// Abort:  根据类型回滚 (插入清空 / 删除恢复 / 更新还原旧数据)
struct WriteRecord {
    rid_t rid;
    int64_t old_end_ts;
    bool is_insert;
    bool is_update = false;
    std::vector<char> old_data;  // UPDATE 回滚用: 修改前的完整行数据
};

// 事务对象: 保存事务的快照信息、状态和写集合
class Transaction {
public:
    // 构造函数: txn_id 事务ID, read_ts 读时间戳(快照点)
    explicit Transaction(txn_id_t txn_id, ts_t read_ts);
    ~Transaction() = default;

    txn_id_t GetTxnId() const { return txn_id_; }
    ts_t GetReadTs() const { return read_ts_; }
    ts_t GetCommitTs() const { return commit_ts_; }
    void SetCommitTs(ts_t ts) { commit_ts_ = ts; }
    TransactionState GetState() const { return state_; }
    void SetState(TransactionState state) { state_ = state; }

    // 向写集合中添加一条修改记录
    void AddWriteRecord(rid_t rid, int64_t old_end_ts, bool is_insert,
                         bool is_update = false,
                         std::vector<char> old_data = {}) {
        write_set_.push_back({rid, old_end_ts, is_insert, is_update, std::move(old_data)});
    }
    const std::vector<WriteRecord> &GetWriteSet() const { return write_set_; }
    void ClearWriteSet() { write_set_.clear(); }

private:
    txn_id_t txn_id_;                    // 事务唯一标识
    ts_t read_ts_;                        // 读时间戳 (快照点，决定了事务能看到哪些版本)
    ts_t commit_ts_ = 0;                  // 提交时间戳 (Commit 时分配)
    TransactionState state_;              // 当前事务状态
    std::vector<WriteRecord> write_set_;  // 写集合: 记录事务所有修改
};

// 事务管理器: 负责事务的创建、提交、中止和版本可见性判断
// 使用原子变量分配全局递增的事务ID和时间戳
class TransactionManager {
public:
    TransactionManager();
    ~TransactionManager();

    // 开始一个新事务: 分配 txn_id 和 read_ts
    Transaction *Begin();
    // 提交事务: 分配 commit_ts，将状态设为 COMMITTED
    bool Commit(Transaction *txn);
    // 中止事务: 将状态设为 ABORTED
    bool Abort(Transaction *txn);
    // 根据事务ID查找事务对象
    Transaction *GetTransaction(txn_id_t txn_id);
    // 获取当前全局时间戳
    ts_t GetCurrentTimestamp() { return next_ts_.load(); }
    // 判断事务是否仍在运行中
    bool IsActive(txn_id_t txn_id) const;
    // MVCC 可见性判断: 检查给定元组版本是否对指定读事务可见
    bool IsVisible(const TupleMeta &meta, ts_t read_ts, txn_id_t reader_txn_id) const;

private:
    std::unordered_map<txn_id_t, Transaction *> transactions_;  // 活跃事务表
    std::atomic<txn_id_t> next_txn_id_{1};  // 下一个事务ID (原子自增)
    std::atomic<ts_t> next_ts_{1};           // 下一个时间戳 (原子自增)
    mutable std::mutex mutex_;               // 保护 transactions_ 的互斥锁
};

} // namespace minidb
