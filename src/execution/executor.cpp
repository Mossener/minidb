// 执行器实现 - 火山模型各算子的具体逻辑

#include "execution/executor.h"

namespace minidb {

// ── SeqScanExecutor ───────────────────────────────────

// 构造函数: 初始化输出模式，扫描位置设为 -1 (未开始)
SeqScanExecutor::SeqScanExecutor(ExecutorContext *ctx, Schema *output_schema)
    : Executor(ctx), output_schema_(output_schema), current_rid_(-1), inited_(false) {}

// Init: 定位到表堆中第一个元组的 RID
bool SeqScanExecutor::Init() {
    current_rid_ = ctx_->table_heap->GetFirstRID(*output_schema_);
    inited_ = true;
    return true;
}

// Next: 获取下一个可见元组
// 流程: 1) 从当前 RID 读取元组
//       2) 移动到下一个 RID
//       3) 如果元组可见则返回 true，否则继续尝试下一个
// 返回 false 表示已扫描完所有元组
bool SeqScanExecutor::Next(Tuple *tuple, rid_t *rid) {
    if (!inited_) Init();
    while (current_rid_ >= 0) {
        *rid = current_rid_;
        *tuple = ctx_->table_heap->GetTuple(*output_schema_, current_rid_,
                                             ctx_->transaction, ctx_->txn_mgr);
        current_rid_ = ctx_->table_heap->GetNextRID(current_rid_, *output_schema_);
        if (!tuple->IsNull()) {
            return true;  // 找到可见元组
        }
    }
    return false;  // 扫描结束
}

// ── InsertExecutor ────────────────────────────────────

InsertExecutor::InsertExecutor(ExecutorContext *ctx, std::vector<Tuple> tuples)
    : Executor(ctx), tuples_(std::move(tuples)), idx_(0) {}

// Init: 重置插入进度
bool InsertExecutor::Init() {
    idx_ = 0;
    return true;
}

// Next: 逐个插入元组
// 每调用一次插入一个元组，同时维护 B+树索引 (如果已创建)
// 返回 false 表示全部插入完毕
bool InsertExecutor::Next(Tuple *tuple, rid_t *rid) {
    if (idx_ >= tuples_.size()) return false;
    rid_t new_rid;
    bool ok = ctx_->table_heap->InsertTuple(tuples_[idx_], &new_rid,
                                             ctx_->transaction);
    // 插入成功后同步更新索引
    if (ok && ctx_->index) {
        int32_t key32;
        memcpy(&key32, tuples_[idx_].GetData(), sizeof(int32_t));
        ctx_->index->Insert(static_cast<int64_t>(key32), new_rid);
    }
    idx_++;
    *rid = new_rid;
    return ok;
}

// ── DeleteExecutor ────────────────────────────────────

DeleteExecutor::DeleteExecutor(ExecutorContext *ctx, Executor *child)
    : Executor(ctx), child_(child) {}

DeleteExecutor::~DeleteExecutor() {
    delete child_;
}

bool DeleteExecutor::Init() {
    return child_->Init();
}

// Next: 从子算子获取元组并执行软删除
// 子算子 (通常是 SeqScan 或 IndexScan) 提供待删除的元组流
// 删除后同步更新索引
bool DeleteExecutor::Next(Tuple *tuple, rid_t *rid) {
    Tuple t;
    rid_t r;
    if (!child_->Next(&t, &r)) return false;  // 子算子无更多元组
    bool ok = ctx_->table_heap->DeleteTuple(r, ctx_->transaction);
    // 删除后从索引中移除对应 key
    if (ok && ctx_->index) {
        int32_t key32;
        memcpy(&key32, t.GetData(), sizeof(int32_t));
        ctx_->index->Remove(static_cast<int64_t>(key32));
    }
    *rid = r;
    return ok;
}

// ── IndexScanExecutor ─────────────────────────────────

IndexScanExecutor::IndexScanExecutor(ExecutorContext *ctx, int64_t key)
    : Executor(ctx), key_(key), value_(0), fetched_(false) {}

// Init: 重置获取状态
bool IndexScanExecutor::Init() {
    fetched_ = false;
    return true;
}

// Next: 通过 B+树索引查找 key，返回对应的元组
// 索引扫描是点查询，最多返回一条结果
bool IndexScanExecutor::Next(Tuple *tuple, rid_t *rid) {
    if (fetched_) return false;  // 已返回过结果
    fetched_ = true;
    if (!ctx_->index->GetValue(key_, &value_)) return false;  // Key 不存在
    *rid = value_;
    *tuple = ctx_->table_heap->GetTuple(*ctx_->schema, value_,
                                         ctx_->transaction, ctx_->txn_mgr);
    return !tuple->IsNull();
}

} // namespace minidb
