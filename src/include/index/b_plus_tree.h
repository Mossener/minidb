#pragma once

#include <vector>
#include <memory>
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
    page_id_t next_id; // for leaf node sibling chain
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
    page_id_t CreateLeafPage();
    page_id_t CreateInternalPage();
    bool IsLeafPage(page_id_t page_id);
    int GetNumKeys(page_id_t page_id);
    void SetNumKeys(page_id_t page_id, int n);

    int64_t GetKeyAt(page_id_t page_id, int idx);
    void SetKeyAt(page_id_t page_id, int idx, int64_t key);
    rid_t GetValueAt(page_id_t page_id, int idx);
    void SetValueAt(page_id_t page_id, int idx, rid_t val);
    page_id_t GetChildAt(page_id_t page_id, int idx);
    void SetChildAt(page_id_t page_id, int idx, page_id_t child);
    page_id_t GetNextPage(page_id_t page_id);
    void SetNextPage(page_id_t page_id, page_id_t next);

    void InsertIntoLeaf(page_id_t leaf_id, int64_t key, rid_t value);
    void InsertIntoParent(page_id_t left_id, int64_t key, page_id_t right_id);
    void SplitLeaf(page_id_t leaf_id);
    void SplitInternal(page_id_t internal_id);
    int FindInsertIndex(page_id_t page_id, int64_t key, bool is_leaf);

    void BorrowOrMerge(page_id_t page_id, int idx_in_parent);
    void MergeLeaf(page_id_t left_id, page_id_t right_id, page_id_t parent_id, int idx);
    void MergeInternal(page_id_t left_id, page_id_t right_id, page_id_t parent_id, int idx, int64_t sep_key);

    page_id_t FindLeafPage(int64_t key, bool for_insert = false);

    BufferPoolManager *bpm_;
    page_id_t root_page_id_;
    std::mutex latch_;
};

} // namespace minidb
