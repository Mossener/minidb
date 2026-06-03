#include "execution/executor.h"

namespace minidb {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *ctx, Schema *output_schema)
    : Executor(ctx), output_schema_(output_schema), current_rid_(-1), inited_(false) {}

bool SeqScanExecutor::Init() {
    current_rid_ = ctx_->table_heap->GetFirstRID(*output_schema_);
    inited_ = true;
    return true;
}

bool SeqScanExecutor::Next(Tuple *tuple, rid_t *rid) {
    if (!inited_) Init();
    while (current_rid_ >= 0) {
        *rid = current_rid_;
        *tuple = ctx_->table_heap->GetTuple(*output_schema_, current_rid_,
                                             ctx_->transaction, ctx_->txn_mgr);
        current_rid_ = ctx_->table_heap->GetNextRID(current_rid_, *output_schema_);
        if (!tuple->IsNull()) {
            return true;
        }
    }
    return false;
}

InsertExecutor::InsertExecutor(ExecutorContext *ctx, std::vector<Tuple> tuples)
    : Executor(ctx), tuples_(std::move(tuples)), idx_(0) {}

bool InsertExecutor::Init() {
    idx_ = 0;
    return true;
}

bool InsertExecutor::Next(Tuple *tuple, rid_t *rid) {
    if (idx_ >= tuples_.size()) return false;
    rid_t new_rid;
    bool ok = ctx_->table_heap->InsertTuple(tuples_[idx_], &new_rid,
                                             ctx_->transaction);
    if (ok && ctx_->index) {
        int32_t key32;
        memcpy(&key32, tuples_[idx_].GetData(), sizeof(int32_t));
        ctx_->index->Insert(static_cast<int64_t>(key32), new_rid);
    }
    idx_++;
    *rid = new_rid;
    return ok;
}

DeleteExecutor::DeleteExecutor(ExecutorContext *ctx, Executor *child)
    : Executor(ctx), child_(child) {}

DeleteExecutor::~DeleteExecutor() {
    delete child_;
}

bool DeleteExecutor::Init() {
    return child_->Init();
}

bool DeleteExecutor::Next(Tuple *tuple, rid_t *rid) {
    Tuple t;
    rid_t r;
    if (!child_->Next(&t, &r)) return false;
    bool ok = ctx_->table_heap->DeleteTuple(r, ctx_->transaction);
    if (ok && ctx_->index) {
        int32_t key32;
        memcpy(&key32, t.GetData(), sizeof(int32_t));
        ctx_->index->Remove(static_cast<int64_t>(key32));
    }
    *rid = r;
    return ok;
}

IndexScanExecutor::IndexScanExecutor(ExecutorContext *ctx, int64_t key)
    : Executor(ctx), key_(key), value_(0), fetched_(false) {}

bool IndexScanExecutor::Init() {
    fetched_ = false;
    return true;
}

bool IndexScanExecutor::Next(Tuple *tuple, rid_t *rid) {
    if (fetched_) return false;
    fetched_ = true;
    if (!ctx_->index->GetValue(key_, &value_)) return false;
    *rid = value_;
    *tuple = ctx_->table_heap->GetTuple(*ctx_->schema, value_,
                                         ctx_->transaction, ctx_->txn_mgr);
    return !tuple->IsNull();
}

} // namespace minidb
