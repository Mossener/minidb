// B+树索引模块 — Latch Crabbing 并发版本
// 用 per-page ReaderWriterLatch 替代全局 mutex:
//   读操作: RLock 耦合遍历 (RLock child → RUnlock parent)
//   写操作: WLock + Crabbing (子节点安全则释放祖先 WLock)

#pragma once

#include <vector>
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
    // ── Latch helpers (RAII-style) ──
    struct PageGuard {
        Page *page;
        page_id_t pid;
        bool is_write;
        BufferPoolManager *bpm;

        PageGuard() : page(nullptr), pid(-1), is_write(false), bpm(nullptr) {}
        PageGuard(Page *p, page_id_t id, bool write, BufferPoolManager *b)
            : page(p), pid(id), is_write(write), bpm(b) {}

        // Move-only
        PageGuard(PageGuard &&other) noexcept
            : page(other.page), pid(other.pid), is_write(other.is_write), bpm(other.bpm) {
            other.page = nullptr;
        }
        PageGuard &operator=(PageGuard &&other) noexcept {
            if (this != &other) {
                release();
                page = other.page; pid = other.pid; is_write = other.is_write; bpm = other.bpm;
                other.page = nullptr;
            }
            return *this;
        }
        PageGuard(const PageGuard &) = delete;
        PageGuard &operator=(const PageGuard &) = delete;

        ~PageGuard() { release(); }

        void release() {
            if (!page) return;
            if (is_write) page->GetLatch().WUnlock();
            else          page->GetLatch().RUnlock();
            bpm->UnpinPage(pid, false);
            page = nullptr;
        }
    };

    PageGuard LockShared(page_id_t pid);  // Fetch + RLock
    PageGuard LockExclusive(page_id_t pid); // Fetch + WLock

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
    // Insert: per-page WLock via path-stack (抄自 PG _bt_split 协议)
    // Read: per-page RLock 耦合遍历
};

} // namespace minidb
