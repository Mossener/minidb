// 表堆(TableHeap)实现 - 元组的物理存储和页面链管理

#include "storage/table_heap.h"

namespace minidb {

// 构造函数: 初始化缓冲池管理器引用，首页ID暂设为无效
TableHeap::TableHeap(BufferPoolManager *bpm)
    : bpm_(bpm), first_page_id_(INVALID_PAGE_ID) {}

// 分配新页面: 在缓冲池中创建新页，初始化 PageHeader，加入页面链表
// 返回: 新页面ID，失败返回 INVALID_PAGE_ID
page_id_t TableHeap::AllocatePage() {
    page_id_t page_id;
    Page *page = bpm_->NewPage(&page_id);  // 在磁盘分配空间 + 缓冲池创建页面
    if (!page) return INVALID_PAGE_ID;
    PageHeader header;
    header.num_tuples = 0;                            // 新页面初始无元组
    header.free_space = PAGE_SIZE - PAGE_HEADER_SIZE;  // 初始空闲 = 页面总大小 - 头部
    header.next_page_id = INVALID_PAGE_ID;             // 默认无后继页
    memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);
    bpm_->UnpinPage(page_id, true);
    return page_id;
}

// InsertTuple: 向表堆中插入一个元组
// 流程: 1) 遍历页面链寻找有足够空闲空间的页面
//       2) 将元组数据写入页面 (raw_size + TupleMeta + raw_data)
//       3) 更新页面头部 (free_space--, num_tuples++)
//       4) 构造 RID (高32位=page_id, 低32位=页内偏移)
//       5) 记录到事务写集合 (用于后续 Commit/Abort)
// 返回: 成功返回 true 并设置 *rid，失败返回 false
bool TableHeap::InsertTuple(const Tuple &tuple, rid_t *rid, Transaction *txn) {
    int raw_size = static_cast<int>(tuple.GetLength());
    int storage_size = sizeof(int32_t) + static_cast<int>(sizeof(TupleMeta)) + raw_size;
    int page_need = storage_size;

    // 元组太大，超过一个页面能容纳的范围，无法插入
    if (page_need > PAGE_SIZE - PAGE_HEADER_SIZE) return false;

    page_id_t page_id = first_page_id_;
    // 如果还没有任何页面，先分配首页
    if (page_id == INVALID_PAGE_ID) {
        page_id = AllocatePage();
        if (page_id == INVALID_PAGE_ID) return false;
        first_page_id_ = page_id;
    }

    while (true) {
        Page *page = bpm_->FetchPage(page_id);
        PageHeader header;
        memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);

        // 当前页面有足够空间，在此页插入
        if (header.free_space >= page_need) {
            // 从空闲空间末尾分配 (free_space 从 PAGE_SIZE 向下递减)
            int offset = PAGE_SIZE - header.free_space;

            // 写入 raw_data_size
            int32_t raw_size_field = raw_size;
            memcpy(page->GetData() + offset, &raw_size_field, sizeof(int32_t));

            // 写入 TupleMeta (begin_ts 初始为 TS_UNCOMMITTED，end_ts 为 TS_MAX)
            TupleMeta meta;
            meta.txn_id = txn->GetTxnId();
            meta.begin_ts = TS_UNCOMMITTED;  // 插入尚未提交
            meta.end_ts = TS_MAX;             // 尚未被删除
            WriteTupleMeta(page->GetData(), offset, meta);

            // 写入序列化的列数据
            tuple.SerializeTo(page->GetData() + offset + TUPLE_META_HEADER_SIZE);

            // 更新页面头部统计
            header.free_space -= page_need;
            header.num_tuples++;
            memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);

            // 构造 RID: 高32位=page_id, 低32位=页内偏移
            *rid = (static_cast<rid_t>(page_id) << 32) | (offset & 0xFFFFFFFF);
            // 记录写操作: 插入类型，old_end_ts 填 TS_MAX
            txn->AddWriteRecord(*rid, TS_MAX, true);
            bpm_->UnpinPage(page_id, true);
            return true;
        }

        // 当前页面空间不足，尝试下一个页面
        page_id_t next = header.next_page_id;
        if (next == INVALID_PAGE_ID) {
            // 链尾，分配新页面扩展链表
            next = AllocatePage();
            if (next == INVALID_PAGE_ID) {
                bpm_->UnpinPage(page_id, false);
                return false;
            }
            header.next_page_id = next;
            memcpy(page->GetData(), &header, PAGE_HEADER_SIZE);
        }
        bpm_->UnpinPage(page_id, false);
        page_id = next;
    }
}

// DeleteTuple: 软删除一个元组 (MVCC 方式)
// 将元组的 end_ts 标记为 TS_UNCOMMITTED，提交时再写入实际 commit_ts
// 保存旧 end_ts 到写集合，以便回滚时恢复
bool TableHeap::DeleteTuple(rid_t rid, Transaction *txn) {
    page_id_t page_id = static_cast<page_id_t>(rid >> 32);      // 从 RID 高32位解码 page_id
    int offset = static_cast<int>(rid & 0xFFFFFFFF);              // 从 RID 低32位解码 页内偏移
    Page *page = bpm_->FetchPage(page_id);
    if (!page) return false;

    TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
    // 记录写操作: 删除类型，保存旧 end_ts 用于 Abort 时恢复
    txn->AddWriteRecord(rid, meta.end_ts, false);
    meta.end_ts = TS_UNCOMMITTED; // 标记为已删除但尚未提交
    WriteTupleMeta(page->GetData(), offset, meta);
    bpm_->UnpinPage(page_id, true);
    return true;
}

// GetTuple: 根据 RID 读取元组，并进行 MVCC 可见性检查
// 如果元组对当前事务不可见，返回空 Tuple (IsNull() == true)
Tuple TableHeap::GetTuple(const Schema &schema, rid_t rid, Transaction *txn,
                          TransactionManager *txn_mgr) {
    page_id_t page_id = static_cast<page_id_t>(rid >> 32);
    int offset = static_cast<int>(rid & 0xFFFFFFFF);
    Page *page = bpm_->FetchPage(page_id);
    if (!page) return Tuple();

    TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
    int raw_size = GetRawDataSize(page->GetData(), offset);

    // MVCC 可见性检查: 不可见则返回空元组
    if (!txn_mgr->IsVisible(meta, txn->GetReadTs(), txn->GetTxnId())) {
        bpm_->UnpinPage(page_id, false);
        return Tuple();
    }

    // 拷贝列数据并构造 Tuple 对象返回
    std::vector<char> data(raw_size);
    memcpy(data.data(), page->GetData() + offset + TUPLE_META_HEADER_SIZE, raw_size);
    bpm_->UnpinPage(page_id, false);
    return Tuple(std::move(data), schema);
}

// GetFirstRID: 返回页面链中第一个有效元组的 RID
// 用于全表扫描的起始位置；从首页的 PAGE_HEADER_SIZE 偏移量开始
// 如果表为空 (无首页或首页无元组)，返回 -1
rid_t TableHeap::GetFirstRID(const Schema &schema) {
    if (first_page_id_ == INVALID_PAGE_ID) return -1;
    page_id_t pid = first_page_id_;
    Page *page = bpm_->FetchPage(pid);
    if (!page) return -1;
    PageHeader header;
    memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);
    if (header.num_tuples <= 0) {
        bpm_->UnpinPage(pid, false);
        return -1;
    }
    // 第一个元组紧跟在 PageHeader 之后
    rid_t rid = (static_cast<rid_t>(pid) << 32) | (PAGE_HEADER_SIZE & 0xFFFFFFFF);
    bpm_->UnpinPage(pid, false);
    return rid;
}

// GetNextRID: 返回当前元组之后下一个元组的 RID
// 通过当前元组的存储大小计算下一个元组的偏移量
// 如果当前页已无更多元组，则跳到下一页继续
// 如果没有更多元组，返回 -1
rid_t TableHeap::GetNextRID(rid_t current_rid, const Schema &schema) {
    page_id_t page_id = static_cast<page_id_t>(current_rid >> 32);
    int offset = static_cast<int>(current_rid & 0xFFFFFFFF);
    Page *page = bpm_->FetchPage(page_id);
    if (!page) return -1;

    // 计算下一个元组的偏移量 = 当前偏移 + 当前元组占用大小
    int storage_size = GetTupleStorageSize(page->GetData(), offset);
    int next_offset = offset + storage_size;

    PageHeader header;
    memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);

    // 同一页内还有后续元组 (next_offset < 已使用空间)
    if (next_offset < PAGE_SIZE - header.free_space) {
        bpm_->UnpinPage(page_id, false);
        return (static_cast<rid_t>(page_id) << 32) | (next_offset & 0xFFFFFFFF);
    }

    // 当前页已无更多元组，跳转到下一页
    if (header.next_page_id == INVALID_PAGE_ID) {
        bpm_->UnpinPage(page_id, false);
        return -1;
    }
    page_id = header.next_page_id;
    page = bpm_->FetchPage(page_id);
    if (!page) return -1;
    memcpy(&header, page->GetData(), PAGE_HEADER_SIZE);
    if (header.num_tuples <= 0) {
        bpm_->UnpinPage(page_id, false);
        return -1;
    }
    // 下一页第一个元组位于 PAGE_HEADER_SIZE 处
    rid_t rid = (static_cast<rid_t>(page_id) << 32) | (PAGE_HEADER_SIZE & 0xFFFFFFFF);
    bpm_->UnpinPage(page_id, false);
    return rid;
}

} // namespace minidb
