#include "common/database.h"

namespace minidb {

MiniDB::MiniDB()
    : disk_manager_("minidb_data"), bpm_(&disk_manager_) {}

MiniDB::~MiniDB() {
    for (auto &[name, info] : tables_) {
        delete info;
    }
    // Cleanup any remaining transactions
    // (TransactionManager owns the Transaction objects via its transactions_ map
    //  but since we forward-declared Transaction in the header, we need access)
}

bool MiniDB::CreateTable(const std::string &name, const Schema &schema) {
    if (tables_.count(name)) return false;
    auto *info = new TableInfo();
    info->name = name;
    info->schema = new Schema(schema);
    info->heap = new TableHeap(&bpm_);
    info->index = nullptr;
    info->has_index = false;
    tables_[name] = info;
    return true;
}

bool MiniDB::DropTable(const std::string &name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) return false;
    delete it->second;
    tables_.erase(it);
    return true;
}

bool MiniDB::CreateIndex(const std::string &table_name) {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return false;
    if (it->second->has_index) return true;
    it->second->index = new BPlusTree(&bpm_);
    it->second->has_index = true;

    TableHeap *heap = it->second->heap;
    Schema *schema = it->second->schema;

    // Use a dummy transaction with TS_MAX to see all committed data
    Transaction *dummy = BeginTxn();
    rid_t rid = heap->GetFirstRID(*schema);
    while (rid >= 0) {
        Tuple t = heap->GetTuple(*schema, rid, dummy, &txn_mgr_);
        if (!t.IsNull()) {
            int32_t key32;
            memcpy(&key32, t.GetData(), sizeof(int32_t));
            it->second->index->Insert(static_cast<int64_t>(key32), rid);
        }
        rid = heap->GetNextRID(rid, *schema);
    }
    CommitTxn(dummy);
    return true;
}

bool MiniDB::Insert(const std::string &table_name, const Tuple &tuple, Transaction *txn) {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return false;
    rid_t rid;
    bool ok = it->second->heap->InsertTuple(tuple, &rid, txn);
    if (ok && it->second->has_index) {
        int32_t key32;
        memcpy(&key32, tuple.GetData(), sizeof(int32_t));
        it->second->index->Insert(static_cast<int64_t>(key32), rid);
    }
    return ok;
}

bool MiniDB::Delete(const std::string &table_name, int64_t key, Transaction *txn) {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return false;
    rid_t rid;
    if (it->second->has_index && it->second->index->GetValue(key, &rid)) {
        it->second->heap->DeleteTuple(rid, txn);
        it->second->index->Remove(key);
        return true;
    }
    Schema *schema = it->second->schema;
    rid_t scan_rid = it->second->heap->GetFirstRID(*schema);
    while (scan_rid >= 0) {
        Tuple t = it->second->heap->GetTuple(*schema, scan_rid, txn, &txn_mgr_);
        if (!t.IsNull()) {
            int32_t k;
            memcpy(&k, t.GetData(), sizeof(int32_t));
            if (static_cast<int64_t>(k) == key) {
                it->second->heap->DeleteTuple(scan_rid, txn);
                return true;
            }
        }
        scan_rid = it->second->heap->GetNextRID(scan_rid, *schema);
    }
    return false;
}

std::vector<Tuple> MiniDB::Scan(const std::string &table_name, Transaction *txn) {
    std::vector<Tuple> result;
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return result;

    TableHeap *heap = it->second->heap;
    Schema *schema = it->second->schema;
    rid_t rid = heap->GetFirstRID(*schema);
    while (rid >= 0) {
        Tuple t = heap->GetTuple(*schema, rid, txn, &txn_mgr_);
        if (!t.IsNull()) {
            result.push_back(t);
        }
        rid = heap->GetNextRID(rid, *schema);
    }
    return result;
}

TableInfo *MiniDB::GetTable(const std::string &name) {
    auto it = tables_.find(name);
    if (it != tables_.end()) return it->second;
    return nullptr;
}

Transaction *MiniDB::BeginTxn() {
    return txn_mgr_.Begin();
}

bool MiniDB::CommitTxn(Transaction *txn) {
    if (txn->GetState() != TransactionState::RUNNING) return false;

    txn_mgr_.Commit(txn);
    ts_t commit_ts = txn->GetCommitTs();

    // Apply write set: update begin_ts/end_ts on committed tuples
    for (const auto &wr : txn->GetWriteSet()) {
        page_id_t page_id = static_cast<page_id_t>(wr.rid >> 32);
        int offset = static_cast<int>(wr.rid & 0xFFFFFFFF);
        Page *page = bpm_.FetchPage(page_id);
        if (!page) continue;

        TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
        if (wr.is_insert) {
            meta.begin_ts = commit_ts;
        } else {
            meta.end_ts = commit_ts;
        }
        WriteTupleMeta(page->GetData(), offset, meta);
        bpm_.UnpinPage(page_id, true);
    }
    return true;
}

bool MiniDB::AbortTxn(Transaction *txn) {
    if (txn->GetState() != TransactionState::RUNNING) return false;
    txn_mgr_.Abort(txn);

    // Rollback write set
    for (const auto &wr : txn->GetWriteSet()) {
        page_id_t page_id = static_cast<page_id_t>(wr.rid >> 32);
        int offset = static_cast<int>(wr.rid & 0xFFFFFFFF);
        Page *page = bpm_.FetchPage(page_id);
        if (!page) continue;

        if (wr.is_insert) {
            // Remove inserted tuple by zeroing it out
            int storage_size = GetTupleStorageSize(page->GetData(), offset);
            memset(page->GetData() + offset, 0, storage_size);
            PageHeader header;
            memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);
            header.num_tuples--;
            header.free_space += storage_size;
            memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);
        } else {
            // Restore old end_ts
            TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
            meta.txn_id = txn->GetTxnId();
            meta.begin_ts = TS_UNCOMMITTED; // will be ignored
            meta.end_ts = wr.old_end_ts;
            WriteTupleMeta(page->GetData(), offset, meta);
        }
        bpm_.UnpinPage(page_id, true);
    }
    return true;
}

} // namespace minidb
