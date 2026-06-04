#include "common/lock_manager.h"

namespace minidb {

bool LockManager::LockX(rid_t rid, txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mu_);

    // 已持有同一个 RID 的锁？直接成功（锁重入）
    auto it = txn_locks_.find(txn_id);
    if (it != txn_locks_.end() && it->second.count(rid)) {
        return true;
    }

    // 检查冲突：该 RID 是否被其他事务持有
    auto holder = lock_table_.find(rid);
    if (holder != lock_table_.end() && holder->second != txn_id) {
        txn_id_t blocker = holder->second;

        // 建立 wait-for 边: 我 → 阻塞者
        wait_for_[txn_id] = blocker;

        // 死锁检测：从自己出发 DFS，看是否回到自己
        std::unordered_set<txn_id_t> visited;
        if (has_cycle_from(txn_id, visited)) {
            // 有死锁，当前事务为 victim
            wait_for_.erase(txn_id);
            return false;
        }
        // 无死锁但冲突，返回 false（简化：不让等待，直接通知调用方稍后重试）
        wait_for_.erase(txn_id);
        return false;
    }

    // 无冲突，分配锁
    lock_table_[rid] = txn_id;
    txn_locks_[txn_id].insert(rid);
    return true;
}

void LockManager::Unlock(rid_t rid, txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = lock_table_.find(rid);
    if (it != lock_table_.end() && it->second == txn_id) {
        lock_table_.erase(it);
    }
    auto txit = txn_locks_.find(txn_id);
    if (txit != txn_locks_.end()) {
        txit->second.erase(rid);
        if (txit->second.empty()) {
            txn_locks_.erase(txit);
        }
    }
}

void LockManager::UnlockAll(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = txn_locks_.find(txn_id);
    if (it != txn_locks_.end()) {
        for (rid_t rid : it->second) {
            lock_table_.erase(rid);
        }
        txn_locks_.erase(it);
    }
    // 清理 wait-for 图中涉及该事务的边
    for (auto wit = wait_for_.begin(); wit != wait_for_.end(); ) {
        if (wit->first == txn_id || wit->second == txn_id) {
            wit = wait_for_.erase(wit);
        } else {
            ++wit;
        }
    }
}

bool LockManager::has_cycle_from(txn_id_t start, std::unordered_set<txn_id_t> &visited) {
    if (visited.count(start)) return start == *visited.begin(); // 回到起点 = 有环
    visited.insert(start);
    auto it = wait_for_.find(start);
    if (it != wait_for_.end()) {
        return has_cycle_from(it->second, visited);
    }
    return false;
}

} // namespace minidb
