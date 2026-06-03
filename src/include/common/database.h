#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/table_heap.h"
#include "storage/schema.h"
#include "storage/tuple_meta.h"
#include "index/b_plus_tree.h"
#include "transaction/transaction.h"
#include "parser/parser.h"

namespace minidb {

struct TableInfo {
    std::string name;
    TableHeap *heap;
    Schema *schema;
    BPlusTree *index;
    bool has_index;

    TableInfo() : heap(nullptr), schema(nullptr), index(nullptr), has_index(false) {}
    ~TableInfo() {
        delete heap;
        delete schema;
        delete index;
    }
};

class MiniDB {
public:
    MiniDB();
    ~MiniDB();

    bool CreateTable(const std::string &name, const Schema &schema);
    bool DropTable(const std::string &name);
    bool CreateIndex(const std::string &table_name);

    // Low-level ops
    bool Insert(const std::string &table_name, const Tuple &tuple, Transaction *txn);
    bool Delete(const std::string &table_name, int64_t key, Transaction *txn);
    std::vector<Tuple> Scan(const std::string &table_name, Transaction *txn);

    // SQL-level ops
    bool ExecCreateTable(const SQLStatement &stmt);
    bool ExecInsert(const SQLStatement &stmt, Transaction *txn);
    std::vector<Tuple> ExecSelect(const SQLStatement &stmt, Transaction *txn);
    int ExecDelete(const SQLStatement &stmt, Transaction *txn);

    TableInfo *GetTable(const std::string &name);
    TransactionManager *GetTxnMgr() { return &txn_mgr_; }

    Transaction *BeginTxn();
    bool CommitTxn(Transaction *txn);
    bool AbortTxn(Transaction *txn);

private:
    Tuple BuildTuple(const std::vector<LiteralValue> &values, const Schema &schema);
    bool EvalCondition(const Tuple &tuple, const Schema &schema,
                       const CompareCondition &cond);

    DiskManager disk_manager_;
    BufferPoolManager bpm_;
    TransactionManager txn_mgr_;
    std::unordered_map<std::string, TableInfo *> tables_;
};

} // namespace minidb
