#pragma once

#include <atomic>
#include <cstring>
#include "common/config.h"
#include "storage/page.h"
#include "storage/tuple.h"
#include "storage/schema.h"
#include "storage/tuple_meta.h"
#include "storage/buffer_pool_manager.h"
#include "transaction/transaction.h"

namespace minidb {

struct PageHeader {
    int num_tuples;
    int free_space;
    int next_page_id;
};

static constexpr int PAGE_HEADER_SIZE = sizeof(PageHeader);

class TableHeap {
public:
    explicit TableHeap(BufferPoolManager *bpm);
    ~TableHeap() = default;

    bool InsertTuple(const Tuple &tuple, rid_t *rid, Transaction *txn);
    bool DeleteTuple(rid_t rid, Transaction *txn);
    Tuple GetTuple(const Schema &schema, rid_t rid, Transaction *txn,
                   TransactionManager *txn_mgr);

    rid_t GetFirstRID(const Schema &schema);
    rid_t GetNextRID(rid_t current_rid, const Schema &schema);

private:
    page_id_t AllocatePage();

    BufferPoolManager *bpm_;
    page_id_t first_page_id_;
};

} // namespace minidb
