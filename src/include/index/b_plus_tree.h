// B+树索引模块 - 持久化在页面上的 B+树实现
// 支持单键的 Insert/Remove/GetValue/RangeScan 操作
// 叶子节点通过 next_id 形成单向链表，支持范围扫描

#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include "common/config.h"
#include "storage/page.h"
#include "storage/buffer_pool_manager.h"

namespace minidb {

// ── B+树参数 ──────────────────────────────────────
static constexpr int BPLUS_ORDER = BPLUS_TREE_DEGREE;      // 阶数 (最大孩子数) = 64
static constexpr int MAX_KEYS = BPLUS_ORDER - 1;            // 节点最大键数 = 63
static constexpr int MIN_KEYS = (BPLUS_ORDER / 2) - 1;      // 节点最小键数 = 31
static constexpr int MAX_CHILDREN = BPLUS_ORDER;            // 最大孩子数 = 64
static constexpr int MIN_CHILDREN = (BPLUS_ORDER / 2);      // 最小孩子数 = 32

// B+树页面头部: 位于每个索引页的起始位置
struct BPlusPageHeader {
    bool is_leaf;           // true=叶子节点, false=内部节点
    int num_keys;           // 当前节点中的键数量
    page_id_t parent_id;    // 父节点页面ID
    page_id_t next_id;      // 叶子节点的兄弟链表指针 (仅叶子节点有效)
};

// B+树: 基于缓冲池页面的持久化索引结构
class BPlusTree {
public:
    explicit BPlusTree(BufferPoolManager *bpm);
    ~BPlusTree() = default;

    // 插入键值对 (key 不可重复)
    bool Insert(int64_t key, rid_t value);
    // 删除键 (及其关联的 RID)
    bool Remove(int64_t key);
    // 点查询: 根据 key 获取 RID
    bool GetValue(int64_t key, rid_t *value);
    // 范围扫描: 返回 [lower, upper] 区间内所有 key 对应的 RID
    std::vector<rid_t> RangeScan(int64_t lower, int64_t upper);

private:
    // ── 页面管理 ──
    page_id_t CreateLeafPage();     // 创建空叶子页面
    page_id_t CreateInternalPage(); // 创建空内部页面
    bool IsLeafPage(page_id_t page_id);
    int GetNumKeys(page_id_t page_id);
    void SetNumKeys(page_id_t page_id, int n);

    // ── 键/值/子节点读写 (按页内偏移索引访问) ──
    int64_t GetKeyAt(page_id_t page_id, int idx);
    void SetKeyAt(page_id_t page_id, int idx, int64_t key);
    rid_t GetValueAt(page_id_t page_id, int idx);
    void SetValueAt(page_id_t page_id, int idx, rid_t val);
    page_id_t GetChildAt(page_id_t page_id, int idx);
    void SetChildAt(page_id_t page_id, int idx, page_id_t child);
    page_id_t GetNextPage(page_id_t page_id);
    void SetNextPage(page_id_t page_id, page_id_t next);

    // ── 插入辅助 ──
    void InsertIntoLeaf(page_id_t leaf_id, int64_t key, rid_t value);
    void InsertIntoParent(page_id_t left_id, int64_t key, page_id_t right_id);
    void SplitLeaf(page_id_t leaf_id);             // 叶子节点分裂
    void SplitInternal(page_id_t internal_id);      // 内部节点分裂
    int FindInsertIndex(page_id_t page_id, int64_t key, bool is_leaf);

    // ── 删除辅助 (借位/合并) ──
    void BorrowOrMerge(page_id_t page_id, int idx_in_parent);
    void MergeLeaf(page_id_t left_id, page_id_t right_id, page_id_t parent_id, int idx);
    void MergeInternal(page_id_t left_id, page_id_t right_id, page_id_t parent_id, int idx, int64_t sep_key);

    // ── 查找 ──
    // 从根开始向下查找 key 所在的叶子页面
    // for_insert=true 时对于重复键插入可能返回右侧页面
    page_id_t FindLeafPage(int64_t key, bool for_insert = false);

    BufferPoolManager *bpm_;     // 缓冲池管理器
    page_id_t root_page_id_;     // 根节点页面ID
    std::mutex latch_;           // 全局互斥锁 (简化版，保护整棵树)
};

} // namespace minidb
