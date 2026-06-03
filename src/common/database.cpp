#include "common/database.h"
#include <cstring>
#include <algorithm>

namespace minidb {

MiniDB::MiniDB()
    : disk_manager_("minidb_data"), bpm_(&disk_manager_) {}

MiniDB::~MiniDB() {
    for (auto &[name, info] : tables_) {
        delete info;
    }
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

// ── SQL-level ops ─────────────────────────────────

static TypeId to_type_id(ColType ct) {
    switch (ct) {
        case ColType::INTEGER: return TypeId::INTEGER;
        case ColType::FLOAT: return TypeId::FLOAT;
        case ColType::VARCHAR: return TypeId::VARCHAR;
        case ColType::BOOLEAN: return TypeId::BOOLEAN;
    }
    return TypeId::INTEGER;
}

bool MiniDB::ExecCreateTable(const SQLStatement &stmt) {
    std::vector<Column> cols;
    for (const auto &cd : stmt.columns) {
        size_t len = (cd.type == ColType::VARCHAR && cd.length > 0) ? cd.length : 0;
        cols.emplace_back(cd.name, to_type_id(cd.type), len, true);
    }
    return CreateTable(stmt.table_name, Schema(std::move(cols)));
}

Tuple MiniDB::BuildTuple(const std::vector<LiteralValue> &values, const Schema &schema) {
    if (values.size() != schema.GetColumnCount()) {
        throw std::runtime_error("Column count mismatch");
    }
    std::vector<char> data(schema.GetSerializedSize(), 0);
    for (size_t i = 0; i < values.size(); i++) {
        size_t offset = schema.GetColumnOffset(i);
        const auto &col = schema.GetColumn(i);

        switch (col.type) {
            case TypeId::INTEGER: {
                int32_t v = static_cast<int32_t>(values[i].AsInt());
                memcpy(data.data() + offset, &v, sizeof(int32_t));
                break;
            }
            case TypeId::FLOAT: {
                double v = values[i].AsFloat();
                memcpy(data.data() + offset, &v, sizeof(double));
                break;
            }
            case TypeId::VARCHAR: {
                std::string s = values[i].AsString();
                size_t copy_len = std::min(s.size(), col.length ? col.length : s.size());
                memcpy(data.data() + offset, s.data(), copy_len);
                break;
            }
            case TypeId::BOOLEAN: {
                std::string s = values[i].AsString();
                bool v = (s == "true" || s == "1");
                data[offset] = v ? 1 : 0;
                break;
            }
        }
    }
    return Tuple(std::move(data), schema);
}

bool MiniDB::EvalCondition(const Tuple &tuple, const Schema &schema,
                           const CompareCondition &cond) {
    if (tuple.IsNull()) return false;

    size_t col_idx = static_cast<size_t>(-1);
    for (size_t i = 0; i < schema.GetColumnCount(); i++) {
        if (schema.GetColumn(i).name == cond.column) {
            col_idx = i;
            break;
        }
    }
    if (col_idx == static_cast<size_t>(-1)) return false;

    size_t offset = schema.GetColumnOffset(col_idx);
    const auto &col = schema.GetColumn(col_idx);
    const char *data = tuple.GetData();

    auto compare = [&](auto lhs, auto rhs, auto op) -> bool {
        switch (op) {
            case CompareCondition::EQ: return lhs == rhs;
            case CompareCondition::NE: return lhs != rhs;
            case CompareCondition::LT: return lhs < rhs;
            case CompareCondition::GT: return lhs > rhs;
            case CompareCondition::LE: return lhs <= rhs;
            case CompareCondition::GE: return lhs >= rhs;
        }
        return false;
    };

    switch (col.type) {
        case TypeId::INTEGER: {
            int32_t lhs;
            memcpy(&lhs, data + offset, sizeof(int32_t));
            return compare(lhs, static_cast<int32_t>(cond.value.AsInt()), cond.op);
        }
        case TypeId::FLOAT: {
            double lhs;
            memcpy(&lhs, data + offset, sizeof(double));
            return compare(lhs, cond.value.AsFloat(), cond.op);
        }
        case TypeId::VARCHAR: {
            std::string lhs(data + offset, col.length);
            // Null-terminate at first \0
            size_t null_pos = lhs.find('\0');
            if (null_pos != std::string::npos) lhs = lhs.substr(0, null_pos);
            return compare(lhs, cond.value.AsString(), cond.op);
        }
        case TypeId::BOOLEAN: {
            bool lhs = (data[offset] != 0);
            bool rhs = (cond.value.AsString() == "true" || cond.value.AsString() == "1");
            return compare(lhs, rhs, cond.op);
        }
    }
    return false;
}

bool MiniDB::ExecInsert(const SQLStatement &stmt, Transaction *txn) {
    auto *tbl = GetTable(stmt.table_name);
    if (!tbl) return false;

    for (const auto &row : stmt.insert_values) {
        Tuple tuple = BuildTuple(row, *tbl->schema);
        rid_t rid;
        bool ok = tbl->heap->InsertTuple(tuple, &rid, txn);
        if (!ok) return false;
        if (tbl->has_index) {
            int32_t key32;
            memcpy(&key32, tuple.GetData(), sizeof(int32_t));
            tbl->index->Insert(static_cast<int64_t>(key32), rid);
        }
    }
    return true;
}

std::vector<Tuple> MiniDB::ExecSelect(const SQLStatement &stmt, Transaction *txn) {
    std::vector<Tuple> result;
    auto *tbl = GetTable(stmt.table_name);
    if (!tbl) return result;

    rid_t rid = tbl->heap->GetFirstRID(*tbl->schema);
    while (rid >= 0) {
        Tuple t = tbl->heap->GetTuple(*tbl->schema, rid, txn, &txn_mgr_);
        if (!t.IsNull()) {
            if (!stmt.where || EvalCondition(t, *tbl->schema, *stmt.where)) {
                result.push_back(t);
            }
        }
        rid = tbl->heap->GetNextRID(rid, *tbl->schema);
    }
    return result;
}

int MiniDB::ExecDelete(const SQLStatement &stmt, Transaction *txn) {
    auto *tbl = GetTable(stmt.table_name);
    if (!tbl) return 0;

    int deleted = 0;
    rid_t rid = tbl->heap->GetFirstRID(*tbl->schema);
    while (rid >= 0) {
        Tuple t = tbl->heap->GetTuple(*tbl->schema, rid, txn, &txn_mgr_);
        rid_t next_rid = tbl->heap->GetNextRID(rid, *tbl->schema);
        if (!t.IsNull() && stmt.where && EvalCondition(t, *tbl->schema, *stmt.where)) {
            tbl->heap->DeleteTuple(rid, txn);
            deleted++;
            if (tbl->has_index) {
                int32_t key32;
                memcpy(&key32, t.GetData(), sizeof(int32_t));
                tbl->index->Remove(static_cast<int64_t>(key32));
            }
        }
        rid = next_rid;
    }
    return deleted;
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

    for (const auto &wr : txn->GetWriteSet()) {
        page_id_t page_id = static_cast<page_id_t>(wr.rid >> 32);
        int offset = static_cast<int>(wr.rid & 0xFFFFFFFF);
        Page *page = bpm_.FetchPage(page_id);
        if (!page) continue;

        if (wr.is_insert) {
            int storage_size = GetTupleStorageSize(page->GetData(), offset);
            memset(page->GetData() + offset, 0, storage_size);
            PageHeader header;
            memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);
            header.num_tuples--;
            header.free_space += storage_size;
            memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);
        } else {
            TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
            meta.end_ts = wr.old_end_ts;
            WriteTupleMeta(page->GetData(), offset, meta);
        }
        bpm_.UnpinPage(page_id, true);
    }
    return true;
}

} // namespace minidb
