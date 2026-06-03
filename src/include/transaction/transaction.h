#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include "common/config.h"
#include "storage/tuple_meta.h"

namespace minidb {

enum class TransactionState { RUNNING, COMMITTED, ABORTED };

struct WriteRecord {
    rid_t rid;
    int64_t old_end_ts;
    bool is_insert;
};

class Transaction {
public:
    explicit Transaction(txn_id_t txn_id, ts_t read_ts);
    ~Transaction() = default;

    txn_id_t GetTxnId() const { return txn_id_; }
    ts_t GetReadTs() const { return read_ts_; }
    ts_t GetCommitTs() const { return commit_ts_; }
    void SetCommitTs(ts_t ts) { commit_ts_ = ts; }
    TransactionState GetState() const { return state_; }
    void SetState(TransactionState state) { state_ = state; }

    void AddWriteRecord(rid_t rid, int64_t old_end_ts, bool is_insert) {
        write_set_.push_back({rid, old_end_ts, is_insert});
    }
    const std::vector<WriteRecord> &GetWriteSet() const { return write_set_; }
    void ClearWriteSet() { write_set_.clear(); }

private:
    txn_id_t txn_id_;
    ts_t read_ts_;
    ts_t commit_ts_ = 0;
    TransactionState state_;
    std::vector<WriteRecord> write_set_;
};

class TransactionManager {
public:
    TransactionManager();
    ~TransactionManager();

    Transaction *Begin();
    bool Commit(Transaction *txn);
    bool Abort(Transaction *txn);
    Transaction *GetTransaction(txn_id_t txn_id);
    ts_t GetCurrentTimestamp() { return next_ts_.load(); }
    bool IsActive(txn_id_t txn_id) const;
    bool IsVisible(const TupleMeta &meta, ts_t read_ts, txn_id_t reader_txn_id) const;

private:
    std::unordered_map<txn_id_t, Transaction *> transactions_;
    std::atomic<txn_id_t> next_txn_id_{1};
    std::atomic<ts_t> next_ts_{1};
    mutable std::mutex mutex_;
};

} // namespace minidb
