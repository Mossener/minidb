#pragma once

#include <memory>
#include <vector>
#include "common/config.h"
#include "storage/tuple.h"
#include "storage/schema.h"
#include "storage/table_heap.h"
#include "storage/buffer_pool_manager.h"
#include "index/b_plus_tree.h"
#include "transaction/transaction.h"

namespace minidb {

struct ExecutorContext {
    Transaction *transaction;
    TransactionManager *txn_mgr;
    BufferPoolManager *bpm;
    TableHeap *table_heap;
    Schema *schema;
    BPlusTree *index;

    ExecutorContext()
        : transaction(nullptr), txn_mgr(nullptr), bpm(nullptr),
          table_heap(nullptr), schema(nullptr), index(nullptr) {}
};

class Executor {
public:
    explicit Executor(ExecutorContext *ctx) : ctx_(ctx) {}
    virtual ~Executor() = default;
    virtual bool Init() = 0;
    virtual bool Next(Tuple *tuple, rid_t *rid) = 0;

protected:
    ExecutorContext *ctx_;
};

class SeqScanExecutor : public Executor {
public:
    SeqScanExecutor(ExecutorContext *ctx, Schema *output_schema);
    bool Init() override;
    bool Next(Tuple *tuple, rid_t *rid) override;
private:
    Schema *output_schema_;
    rid_t current_rid_;
    bool inited_;
};

class InsertExecutor : public Executor {
public:
    InsertExecutor(ExecutorContext *ctx, std::vector<Tuple> tuples);
    bool Init() override;
    bool Next(Tuple *tuple, rid_t *rid) override;
private:
    std::vector<Tuple> tuples_;
    size_t idx_;
};

class DeleteExecutor : public Executor {
public:
    DeleteExecutor(ExecutorContext *ctx, Executor *child);
    bool Init() override;
    bool Next(Tuple *tuple, rid_t *rid) override;
    ~DeleteExecutor() override;
private:
    Executor *child_;
};

class IndexScanExecutor : public Executor {
public:
    IndexScanExecutor(ExecutorContext *ctx, int64_t key);
    bool Init() override;
    bool Next(Tuple *tuple, rid_t *rid) override;
private:
    int64_t key_;
    rid_t value_;
    bool fetched_;
};

} // namespace minidb
