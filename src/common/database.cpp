// 数据库主模块实现 - MiniDB 的建表、增删查、SQL执行和事务提交/回滚逻辑

#include "common/database.h"
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <unistd.h>

namespace minidb {

// 构造函数: 初始化磁盘管理器、缓冲池、日志管理器，然后执行崩溃恢复
MiniDB::MiniDB()
    : disk_manager_("minidb_data"), bpm_(&disk_manager_),
      log_mgr_("minidb_data.wal") {
    DoRecover();
}

// 析构函数: 释放所有已注册的表及其组件
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
    if (ok) {
        if (it->second->has_index) {
            int32_t key32;
            memcpy(&key32, tuple.GetData(), sizeof(int32_t));
            it->second->index->Insert(static_cast<int64_t>(key32), rid);
        }
        // WAL: 记录 INSERT 日志（完整 tuple 页数据，用于崩溃恢复）
        page_id_t pid = static_cast<page_id_t>(rid >> 32);
        int off = static_cast<int>(rid & 0xFFFFFFFF);
        Page *p = bpm_.FetchPage(pid);
        if (p) {
            int sz = GetTupleStorageSize(p->GetData(), off);
            int buf_sz = sizeof(int64_t) + sizeof(int32_t) + sz;
            std::vector<char> buf(buf_sz);
            memcpy(buf.data(), &rid, 8);
            memcpy(buf.data() + 8, &sz, 4);
            memcpy(buf.data() + 12, p->GetData() + off, sz);
            log_mgr_.Append(txn->GetTxnId(), LogRecordType::INSERT,
                           buf.data(), static_cast<int32_t>(buf_sz));
            bpm_.UnpinPage(pid, false);
        }
    }
    return ok;
}

bool MiniDB::Delete(const std::string &table_name, int64_t key, Transaction *txn) {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return false;
    rid_t rid;
    if (it->second->has_index && it->second->index->GetValue(key, &rid)) {
        // 行级锁: 先锁后删
        if (!lock_mgr_.LockX(rid, txn->GetTxnId())) return false;
        it->second->heap->DeleteTuple(rid, txn);
        it->second->index->Remove(key);
        // WAL: 记录 DELETE 日志
        const auto &wr = txn->GetWriteSet().back();
        char buf[16];
        memcpy(buf, &rid, 8);
        memcpy(buf + 8, &wr.old_end_ts, 8);
        log_mgr_.Append(txn->GetTxnId(), LogRecordType::DELETE, buf, 16);
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
                // 行级锁
                if (!lock_mgr_.LockX(scan_rid, txn->GetTxnId())) return false;
                it->second->heap->DeleteTuple(scan_rid, txn);
                // WAL: 记录 DELETE 日志
                const auto &wr = txn->GetWriteSet().back();
                char buf[16];
                memcpy(buf, &scan_rid, 8);
                memcpy(buf + 8, &wr.old_end_ts, 8);
                log_mgr_.Append(txn->GetTxnId(), LogRecordType::DELETE, buf, 16);
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

// ── SQL 层操作 (AST -> 底层存储) ──────────────────

// 将解析器的列类型枚举 (ColType) 转换为存储层的类型枚举 (TypeId)
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
        // WAL: 记录 INSERT 日志
        page_id_t pid = static_cast<page_id_t>(rid >> 32);
        int off = static_cast<int>(rid & 0xFFFFFFFF);
        Page *p = bpm_.FetchPage(pid);
        if (p) {
            int sz = GetTupleStorageSize(p->GetData(), off);
            int buf_sz = sizeof(int64_t) + sizeof(int32_t) + sz;
            std::vector<char> buf(buf_sz);
            memcpy(buf.data(), &rid, 8);
            memcpy(buf.data() + 8, &sz, 4);
            memcpy(buf.data() + 12, p->GetData() + off, sz);
            log_mgr_.Append(txn->GetTxnId(), LogRecordType::INSERT,
                           buf.data(), static_cast<int32_t>(buf_sz));
            bpm_.UnpinPage(pid, false);
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
            // 行级锁
            if (!lock_mgr_.LockX(rid, txn->GetTxnId())) continue;
            tbl->heap->DeleteTuple(rid, txn);
            // WAL: 记录 DELETE 日志
            const auto &wr = txn->GetWriteSet().back();
            char buf[16];
            memcpy(buf, &rid, 8);
            memcpy(buf + 8, &wr.old_end_ts, 8);
            log_mgr_.Append(txn->GetTxnId(), LogRecordType::DELETE, buf, 16);
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

// BeginTxn: 向事务管理器请求创建新事务
Transaction *MiniDB::BeginTxn() {
    return txn_mgr_.Begin();
}

// CommitTxn: 提交事务 — WAL 协议核心
// 步骤: ① 分配 commit_ts
//      ② 写 COMMIT 日志 + fsync (WAL 关键: 日志必须先于数据页落盘)
//      ③ 更新数据页上的 begin_ts / end_ts (崩溃恢复时从日志重放本步)
bool MiniDB::CommitTxn(Transaction *txn) {
    if (txn->GetState() != TransactionState::RUNNING) return false;

    // ① 分配提交时间戳
    txn_mgr_.Commit(txn);
    ts_t commit_ts = txn->GetCommitTs();

    // ② WAL: 先 fsync 日志，确保 COMMIT 记录已持久化（含 commit_ts，供恢复使用）
    int64_t cts = commit_ts;
    log_mgr_.Append(txn->GetTxnId(), LogRecordType::COMMIT,
                    reinterpret_cast<const char*>(&cts), sizeof(int64_t));
    log_mgr_.Flush();

    // ③ 日志已安全落盘，现在更新数据页上的元数据
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
    lock_mgr_.UnlockAll(txn->GetTxnId());  // 提交时释放行级锁
    return true;
}

// AbortTxn: 中止事务，回滚写集合中的所有修改
// 流程: 1) 校验事务状态为 RUNNING
//       2) 调用 txn_mgr_.Abort 标记事务为 ABORTED
//       3) 遍历写集合，根据操作类型执行回滚:
//          - 插入: 清零存储空间，回收页面空闲空间，减少元组计数
//          - 删除: 恢复旧 end_ts (撤销软删除标记)
bool MiniDB::AbortTxn(Transaction *txn) {
    if (txn->GetState() != TransactionState::RUNNING) return false;
    txn_mgr_.Abort(txn);

    for (const auto &wr : txn->GetWriteSet()) {
        page_id_t page_id = static_cast<page_id_t>(wr.rid >> 32);
        int offset = static_cast<int>(wr.rid & 0xFFFFFFFF);
        Page *page = bpm_.FetchPage(page_id);
        if (!page) continue;

        if (wr.is_insert) {
            // 插入回滚: 清零整个元组存储区域，回收空间
            int storage_size = GetTupleStorageSize(page->GetData(), offset);
            memset(page->GetData() + offset, 0, storage_size);
            PageHeader header;
            memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);
            header.num_tuples--;               // 元组计数减一
            header.free_space += storage_size; // 归还空闲空间
            memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);
        } else {
            // 删除回滚: 恢复原始的 end_ts (通常是 TS_MAX)
            TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
            meta.end_ts = wr.old_end_ts;
            WriteTupleMeta(page->GetData(), offset, meta);
        }
        bpm_.UnpinPage(page_id, true);
    }
    lock_mgr_.UnlockAll(txn->GetTxnId());  // 回滚时释放行级锁
    return true;
}

// DoRecover: 启动时从 WAL 恢复未持久化的已提交操作
// 流程: ① 扫描 WAL → 找出所有 COMMIT 记录，建立 txn_id→commit_ts 映射
//      ② 重放已提交事务的 INSERT（写回 page + 更新 begin_ts）和 DELETE（更新 end_ts）
//      ③ 截断 WAL 文件
void MiniDB::DoRecover() {
    auto records = log_mgr_.Recover();
    if (records.empty()) return;

    // ① 从 COMMIT 记录中提取每个事务的 commit_ts
    std::unordered_map<int64_t, int64_t> commit_ts_map;
    for (const auto &rec : records) {
        if (rec.type == LogRecordType::COMMIT && rec.data.size() >= 8) {
            int64_t cts;
            memcpy(&cts, rec.data.data(), 8);
            commit_ts_map[rec.txn_id] = cts;
        }
    }

    // ② 重放 INSERT / DELETE
    for (const auto &rec : records) {
        int64_t cts = commit_ts_map[rec.txn_id];

        if (rec.type == LogRecordType::INSERT) {
            rid_t rid;
            int32_t sz;
            memcpy(&rid, rec.data.data(), 8);
            memcpy(&sz, rec.data.data() + 8, 4);

            page_id_t pid = static_cast<page_id_t>(rid >> 32);
            int off = static_cast<int>(rid & 0xFFFFFFFF);

            Page *page = bpm_.FetchPage(pid);
            if (!page) { page = bpm_.NewPage(&pid); }
            if (page) {
                // 写回元组数据（含未提交的 begin_ts）
                memcpy(page->GetData() + off, rec.data.data() + 12, sz);
                // 更新 begin_ts 为 commit_ts
                TupleMeta meta = ReadTupleMeta(page->GetData(), off);
                meta.begin_ts = cts;
                WriteTupleMeta(page->GetData(), off, meta);
                // 更新页面头
                PageHeader header;
                memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);
                header.num_tuples++;
                header.free_space -= sz;
                memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);
                bpm_.UnpinPage(pid, true);
            }
        } else if (rec.type == LogRecordType::DELETE) {
            rid_t rid;
            memcpy(&rid, rec.data.data(), 8);
            page_id_t pid = static_cast<page_id_t>(rid >> 32);
            int off = static_cast<int>(rid & 0xFFFFFFFF);

            Page *page = bpm_.FetchPage(pid);
            if (page) {
                TupleMeta meta = ReadTupleMeta(page->GetData(), off);
                meta.end_ts = cts;  // 将未提交删除变为已提交删除
                WriteTupleMeta(page->GetData(), off, meta);
                bpm_.UnpinPage(pid, true);
            }
        }
    }

    // ③ 清空 WAL（恢复完成后无需保留）
    log_mgr_.Flush();
    ::truncate("minidb_data.wal", 0);
}

} // namespace minidb
