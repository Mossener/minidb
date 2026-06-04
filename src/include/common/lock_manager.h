#pragma once

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include "common/config.h"

namespace minidb {

// LockManager: 行级排他锁 + 死锁检测
// 每条记录 (RID) 同时只能被一个事务持有 X 锁。
// 如果请求冲突，事务进入等待，并触发死锁检测。

class LockManager {
public:
    LockManager() = default;

    // 尝试获取 RID 的排他锁。成功返回 true；若冲突，检测死锁：
    // 若无死锁 → 阻塞等待（简化：直接返回 false 由调用方 abort）
    // 若有死锁 → 当前事务被选为 victim，返回 false
    bool LockX(rid_t rid, txn_id_t txn_id);

    // 释放指定 RID 上的锁
    void Unlock(rid_t rid, txn_id_t txn_id);

    // 释放某个事务持有的全部锁（Commit / Abort 时调用）
    void UnlockAll(txn_id_t txn_id);

private:
    // rid → 当前持有锁的事务 ID
    std::unordered_map<rid_t, txn_id_t> lock_table_;

    // txn_id → 该事务持有的所有 RID 集合
    std::unordered_map<txn_id_t, std::unordered_set<rid_t>> txn_locks_;

    // wait-for graph: 等待者 → 被等待者
    // txn_A 等待 txn_B 释放 rid → 有向边 A → B
    std::unordered_map<txn_id_t, txn_id_t> wait_for_;

    std::mutex mu_;

    // 死锁检测: 从 start_txn 出发 DFS，如果回到 start_txn 则有环
    bool has_cycle_from(txn_id_t start_txn, std::unordered_set<txn_id_t> &visited);
};

} // namespace minidb
