// 表堆(TableHeap)模块 - 管理表的页面链表结构，提供元组的增删查操作
// 数据页之间通过 next_page_id 形成单向链表，每个页面头部记录元组数量和空闲空间

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

// 页面头部结构: 位于每个数据页的起始位置，记录该页的元信息
struct PageHeader {
    int num_tuples;      // 当前页面中的元组数量
    int free_space;       // 当前页面的剩余空闲空间 (从页面末尾向上增长)
    int next_page_id;     // 下一页的页面ID，-1 表示链表末尾
};

static constexpr int PAGE_HEADER_SIZE = sizeof(PageHeader);

// 表堆: 通过页面链表管理一张表的所有元组数据
class TableHeap {
public:
    explicit TableHeap(BufferPoolManager *bpm);
    ~TableHeap() = default;

    // 插入元组: 找到有足够空间的页面写入，返回分配的 RID
    bool InsertTuple(const Tuple &tuple, rid_t *rid, Transaction *txn);
    // 删除元组: 标记元组的 end_ts 为未提交状态 (软删除)
    bool DeleteTuple(rid_t rid, Transaction *txn);
    // 读取元组: 根据 RID 定位并反序列化元组，做 MVCC 可见性检查
    Tuple GetTuple(const Schema &schema, rid_t rid, Transaction *txn,
                   TransactionManager *txn_mgr);

    // 获取链表中第一个有效元组的 RID (用于全表扫描)
    rid_t GetFirstRID(const Schema &schema);
    // 获取当前 RID 之后下一个元组的 RID (跨页边界自动跳转)
    rid_t GetNextRID(rid_t current_rid, const Schema &schema);

private:
    // 分配新页面: 初始化 PageHeader，设置空闲空间和链表指针
    page_id_t AllocatePage();

    BufferPoolManager *bpm_;           // 缓冲池管理器，用于获取/释放页面
    page_id_t first_page_id_;          // 链表的首页ID
};

} // namespace minidb
