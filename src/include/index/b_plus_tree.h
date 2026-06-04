// B+树索引模块 — Latch Crabbing 并发版本
// 用 per-page ReaderWriterLatch 替代全局 mutex:
//   读操作: RLock 耦合遍历 (RLock child → RUnlock parent)
//   写操作: WLock + Crabbing (子节点安全则释放祖先 WLock)

#pragma once

#include <vector>
#include <mutex>
#include "common/config.h"
#include "storage/page.h"
#include "storage/buffer_pool_manager.h"

namespace minidb {

static constexpr int BPLUS_ORDER = BPLUS_TREE_DEGREE;
static constexpr int MAX_KEYS = BPLUS_ORDER - 1;
static constexpr int MIN_KEYS = (BPLUS_ORDER / 2) - 1;
static constexpr int MAX_CHILDREN = BPLUS_ORDER;
static constexpr int MIN_CHILDREN = (BPLUS_ORDER / 2);

struct BPlusPageHeader {
    bool is_leaf;
    int num_keys;
    page_id_t parent_id;
    page_id_t next_id;
};

class BPlusTree {
public:
    explicit BPlusTree(BufferPoolManager *bpm);
    ~BPlusTree() = default;

    bool Insert(int64_t key, rid_t value);
    bool Remove(int64_t key);
    bool GetValue(int64_t key, rid_t *value);
    std::vector<rid_t> RangeScan(int64_t lower, int64_t upper);

private:
    // ── Latch helpers ──
    struct LatchedPage {
        Page *page;
        page_id_t pid;
        bool is_write;
        void RUnlock() { page->GetLatch().RUnlock(); }
        void WUnlock() { page->GetLatch().WUnlock(); }
    };
    LatchedPage RLockPage(page_id_t pid);   // Fetch + RLock
    LatchedPage WLockPage(page_id_t pid);   // Fetch + WLock
    void UnpinUnlock(LatchedPage &lp, bool dirty);

    // ── Safety checks for crabbing ──
    bool IsSafeForInsert(LatchedPage &lp);   // num_keys < MAX_KEYS - 1
    bool IsSafeForRemove(LatchedPage &lp);   // num_keys > MIN_KEYS

    // ── Page management ──
    page_id_t CreateLeafPage();
    page_id_t CreateInternalPage();
    int GetNumKeys(page_id_t page_id);

    // ── Key/value/child access (assumes latch held) ──
    int64_t GetKeyAt(page_id_t page_id, int idx);
    void SetKeyAt(page_id_t page_id, int idx, int64_t key);
    rid_t GetValueAt(page_id_t page_id, int idx);
    void SetValueAt(page_id_t page_id, int idx, rid_t val);
    page_id_t GetChildAt(page_id_t page_id, int idx);
    void SetChildAt(page_id_t page_id, int idx, page_id_t child);
    page_id_t GetNextPage(page_id_t page_id);
    void SetNextPage(page_id_t page_id, page_id_t next);

    // ── Insert helpers ──
    void InsertIntoLeaf(page_id_t leaf_id, int64_t key, rid_t value);
    void InsertIntoParent(page_id_t left_id, int64_t key, page_id_t right_id);
    void SplitLeaf(page_id_t leaf_id);
    void SplitInternal(page_id_t internal_id);

    // ── Remove ──
    bool RemoveFromLeaf(page_id_t leaf_id, int64_t key);

    BufferPoolManager *bpm_;
    page_id_t root_page_id_;
    std::recursive_mutex write_mutex_;  // 写操作全局锁（可重入，供递归 Insert）
    // 读操作使用 per-page ReaderWriterLatch（GetValue/RangeScan）
};

} // namespace minidb
