#include "transaction/transaction.h"

namespace minidb {

Transaction::Transaction(txn_id_t txn_id, ts_t read_ts)
    : txn_id_(txn_id), read_ts_(read_ts), state_(TransactionState::RUNNING) {}

TransactionManager::TransactionManager() {}

TransactionManager::~TransactionManager() {
    for (auto &[_, txn] : transactions_) {
        delete txn;
    }
}

Transaction *TransactionManager::Begin() {
    txn_id_t txn_id = next_txn_id_++;
    ts_t read_ts = next_ts_++;
    auto *txn = new Transaction(txn_id, read_ts);
    std::lock_guard<std::mutex> lock(mutex_);
    transactions_[txn_id] = txn;
    return txn;
}

bool TransactionManager::Commit(Transaction *txn) {
    ts_t commit_ts = next_ts_++;
    txn->SetCommitTs(commit_ts);
    txn->SetState(TransactionState::COMMITTED);
    return true;
}

bool TransactionManager::Abort(Transaction *txn) {
    txn->SetState(TransactionState::ABORTED);
    return true;
}

Transaction *TransactionManager::GetTransaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txn_id);
    if (it != transactions_.end()) return it->second;
    return nullptr;
}

bool TransactionManager::IsActive(txn_id_t txn_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txn_id);
    if (it == transactions_.end()) return false;
    return it->second->GetState() == TransactionState::RUNNING;
}

bool TransactionManager::IsVisible(const TupleMeta &meta, ts_t read_ts, txn_id_t reader_txn_id) const {
    // Own uncommitted writes are always visible
    if (meta.begin_ts == TS_UNCOMMITTED && meta.txn_id == reader_txn_id) {
        return true;
    }
    // Committed: begin_ts must be <= read_ts
    // and (end_ts must be unset or > read_ts)
    if (meta.begin_ts == TS_UNCOMMITTED) return false;
    if (meta.begin_ts > read_ts) return false;
    if (meta.end_ts != TS_MAX && meta.end_ts <= read_ts) return false;
    if (IsActive(meta.txn_id)) return false;
    return true;
}

} // namespace minidb
