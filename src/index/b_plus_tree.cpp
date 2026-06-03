#include "index/b_plus_tree.h"
#include <cstring>
#include <algorithm>
#include <cassert>

namespace minidb {

BPlusTree::BPlusTree(BufferPoolManager *bpm)
    : bpm_(bpm), root_page_id_(INVALID_PAGE_ID) {}

page_id_t BPlusTree::CreateLeafPage() {
    page_id_t page_id;
    Page *page = bpm_->NewPage(&page_id);
    if (!page) return INVALID_PAGE_ID;
    BPlusPageHeader header{true, 0, INVALID_PAGE_ID, INVALID_PAGE_ID};
    memcpy(page->GetData(), &header, sizeof(BPlusPageHeader));
    memset(page->GetData() + sizeof(BPlusPageHeader), 0, PAGE_SIZE - sizeof(BPlusPageHeader));
    bpm_->UnpinPage(page_id, true);
    return page_id;
}

page_id_t BPlusTree::CreateInternalPage() {
    page_id_t page_id;
    Page *page = bpm_->NewPage(&page_id);
    if (!page) return INVALID_PAGE_ID;
    BPlusPageHeader header{false, 0, INVALID_PAGE_ID, INVALID_PAGE_ID};
    memcpy(page->GetData(), &header, sizeof(BPlusPageHeader));
    memset(page->GetData() + sizeof(BPlusPageHeader), 0, PAGE_SIZE - sizeof(BPlusPageHeader));
    bpm_->UnpinPage(page_id, true);
    return page_id;
}

bool BPlusTree::IsLeafPage(page_id_t page_id) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusPageHeader header;
    memcpy(&header, page->GetData(), sizeof(BPlusPageHeader));
    bpm_->UnpinPage(page_id, false);
    return header.is_leaf;
}

int BPlusTree::GetNumKeys(page_id_t page_id) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusPageHeader header;
    memcpy(&header, page->GetData(), sizeof(BPlusPageHeader));
    bpm_->UnpinPage(page_id, false);
    return header.num_keys;
}

void BPlusTree::SetNumKeys(page_id_t page_id, int n) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusPageHeader header;
    memcpy(&header, page->GetData(), sizeof(BPlusPageHeader));
    header.num_keys = n;
    memcpy(page->GetData(), &header, sizeof(BPlusPageHeader));
    bpm_->UnpinPage(page_id, true);
}

int64_t BPlusTree::GetKeyAt(page_id_t page_id, int idx) {
    Page *page = bpm_->FetchPage(page_id);
    int64_t key;
    memcpy(&key, page->GetData() + sizeof(BPlusPageHeader) + idx * sizeof(int64_t), sizeof(int64_t));
    bpm_->UnpinPage(page_id, false);
    return key;
}

void BPlusTree::SetKeyAt(page_id_t page_id, int idx, int64_t key) {
    Page *page = bpm_->FetchPage(page_id);
    memcpy(page->GetData() + sizeof(BPlusPageHeader) + idx * sizeof(int64_t), &key, sizeof(int64_t));
    bpm_->UnpinPage(page_id, true);
}

rid_t BPlusTree::GetValueAt(page_id_t page_id, int idx) {
    int key_area = MAX_KEYS * sizeof(int64_t);
    Page *page = bpm_->FetchPage(page_id);
    rid_t val;
    memcpy(&val, page->GetData() + sizeof(BPlusPageHeader) + key_area + idx * sizeof(rid_t), sizeof(rid_t));
    bpm_->UnpinPage(page_id, false);
    return val;
}

void BPlusTree::SetValueAt(page_id_t page_id, int idx, rid_t val) {
    int key_area = MAX_KEYS * sizeof(int64_t);
    Page *page = bpm_->FetchPage(page_id);
    memcpy(page->GetData() + sizeof(BPlusPageHeader) + key_area + idx * sizeof(rid_t), &val, sizeof(rid_t));
    bpm_->UnpinPage(page_id, true);
}

page_id_t BPlusTree::GetChildAt(page_id_t page_id, int idx) {
    int key_area = MAX_KEYS * sizeof(int64_t);
    int val_area = MAX_KEYS * sizeof(rid_t);
    Page *page = bpm_->FetchPage(page_id);
    page_id_t child;
    memcpy(&child, page->GetData() + sizeof(BPlusPageHeader) + key_area + val_area + idx * sizeof(page_id_t), sizeof(page_id_t));
    bpm_->UnpinPage(page_id, false);
    return child;
}

void BPlusTree::SetChildAt(page_id_t page_id, int idx, page_id_t child) {
    int key_area = MAX_KEYS * sizeof(int64_t);
    int val_area = MAX_KEYS * sizeof(rid_t);
    Page *page = bpm_->FetchPage(page_id);
    memcpy(page->GetData() + sizeof(BPlusPageHeader) + key_area + val_area + idx * sizeof(page_id_t), &child, sizeof(page_id_t));

    BPlusPageHeader header;
    memcpy(&header, page->GetData(), sizeof(BPlusPageHeader));
    header.num_keys = std::max(header.num_keys, idx);
    memcpy(page->GetData(), &header, sizeof(BPlusPageHeader));

    bpm_->UnpinPage(page_id, true);
}

page_id_t BPlusTree::GetNextPage(page_id_t page_id) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusPageHeader header;
    memcpy(&header, page->GetData(), sizeof(BPlusPageHeader));
    bpm_->UnpinPage(page_id, false);
    return header.next_id;
}

void BPlusTree::SetNextPage(page_id_t page_id, page_id_t next) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusPageHeader header;
    memcpy(&header, page->GetData(), sizeof(BPlusPageHeader));
    header.next_id = next;
    memcpy(page->GetData(), &header, sizeof(BPlusPageHeader));
    bpm_->UnpinPage(page_id, true);
}

page_id_t BPlusTree::FindLeafPage(int64_t key, bool for_insert) {
    std::lock_guard<std::mutex> lock(latch_);
    if (root_page_id_ == INVALID_PAGE_ID) return INVALID_PAGE_ID;

    page_id_t current = root_page_id_;
    while (!IsLeafPage(current)) {
        int nk = GetNumKeys(current);
        int i = 0;
        while (i < nk && GetKeyAt(current, i) <= key) i++;
        page_id_t child = GetChildAt(current, i);
        if (for_insert && GetNumKeys(current) == MAX_KEYS) {
            SplitInternal(current);
        }
        current = child;
    }
    return current;
}

bool BPlusTree::Insert(int64_t key, rid_t value) {
    std::lock_guard<std::mutex> lock(latch_);

    if (root_page_id_ == INVALID_PAGE_ID) {
        page_id_t pid = CreateLeafPage();
        if (pid == INVALID_PAGE_ID) return false;
        root_page_id_ = pid;
        SetKeyAt(pid, 0, key);
        SetValueAt(pid, 0, value);
        SetNumKeys(pid, 1);
        return true;
    }

    page_id_t leaf_id = root_page_id_;
    while (!IsLeafPage(leaf_id)) {
        leaf_id = root_page_id_;
        while (!IsLeafPage(leaf_id)) {
            int nk = GetNumKeys(leaf_id);
            int i = 0;
            while (i < nk && GetKeyAt(leaf_id, i) <= key) i++;
            page_id_t child = GetChildAt(leaf_id, i);

            if (GetNumKeys(child) == MAX_KEYS) {
                if (IsLeafPage(child)) {
                    SplitLeaf(child);
                } else {
                    SplitInternal(child);
                }
                int nk_parent = GetNumKeys(leaf_id);
                int j = 0;
                while (j < nk_parent && GetKeyAt(leaf_id, j) <= key) j++;
                child = GetChildAt(leaf_id, j);
            }
            leaf_id = child;
        }
    }

    int nk = GetNumKeys(leaf_id);
    if (nk >= MAX_KEYS) {
        SplitLeaf(leaf_id);
        // Re-traverse from root to find the correct leaf
        leaf_id = root_page_id_;
        while (!IsLeafPage(leaf_id)) {
            int nnk = GetNumKeys(leaf_id);
            int i = 0;
            while (i < nnk && GetKeyAt(leaf_id, i) <= key) i++;
            leaf_id = GetChildAt(leaf_id, i);
        }
    }
    InsertIntoLeaf(leaf_id, key, value);
    return true;
}

void BPlusTree::InsertIntoLeaf(page_id_t leaf_id, int64_t key, rid_t value) {
    int nk = GetNumKeys(leaf_id);
    int i = 0;
    while (i < nk && GetKeyAt(leaf_id, i) < key) i++;
    if (i < nk && GetKeyAt(leaf_id, i) == key) return;

    for (int j = nk; j > i; j--) {
        SetKeyAt(leaf_id, j, GetKeyAt(leaf_id, j - 1));
        SetValueAt(leaf_id, j, GetValueAt(leaf_id, j - 1));
    }
    SetKeyAt(leaf_id, i, key);
    SetValueAt(leaf_id, i, value);
    SetNumKeys(leaf_id, nk + 1);
}

void BPlusTree::SplitLeaf(page_id_t leaf_id) {
    Page *page = bpm_->FetchPage(leaf_id);
    BPlusPageHeader header;
    memcpy(&header, page->GetData(), sizeof(BPlusPageHeader));
    bpm_->UnpinPage(leaf_id, false);

    page_id_t new_id = CreateLeafPage();
    if (new_id == INVALID_PAGE_ID) return;

    int total = header.num_keys;
    int split = total / 2;

    for (int i = split; i < total; i++) {
        int64_t k = GetKeyAt(leaf_id, i);
        rid_t v = GetValueAt(leaf_id, i);
        SetKeyAt(new_id, i - split, k);
        SetValueAt(new_id, i - split, v);
    }
    SetNumKeys(leaf_id, split);
    SetNumKeys(new_id, total - split);

    SetNextPage(new_id, GetNextPage(leaf_id));
    SetNextPage(leaf_id, new_id);

    int64_t up_key = GetKeyAt(new_id, 0);
    InsertIntoParent(leaf_id, up_key, new_id);
}

void BPlusTree::SplitInternal(page_id_t internal_id) {
    Page *page = bpm_->FetchPage(internal_id);
    BPlusPageHeader header;
    memcpy(&header, page->GetData(), sizeof(BPlusPageHeader));
    bpm_->UnpinPage(internal_id, false);

    page_id_t new_id = CreateInternalPage();
    if (new_id == INVALID_PAGE_ID) return;

    int total = header.num_keys;
    int split = total / 2;
    int64_t up_key = GetKeyAt(internal_id, split);

    for (int i = split + 1; i < total; i++) {
        SetKeyAt(new_id, i - split - 1, GetKeyAt(internal_id, i));
        SetChildAt(new_id, i - split - 1, GetChildAt(internal_id, i));
    }
    SetChildAt(new_id, total - split - 1, GetChildAt(internal_id, total));

    SetNumKeys(internal_id, split);
    SetNumKeys(new_id, total - split - 1);

    InsertIntoParent(internal_id, up_key, new_id);
}

void BPlusTree::InsertIntoParent(page_id_t left_id, int64_t key, page_id_t right_id) {
    Page *left_page = bpm_->FetchPage(left_id);
    BPlusPageHeader left_header;
    memcpy(&left_header, left_page->GetData(), sizeof(BPlusPageHeader));
    bpm_->UnpinPage(left_id, false);

    page_id_t parent_id = left_header.parent_id;

    if (parent_id == INVALID_PAGE_ID) {
        page_id_t new_root = CreateInternalPage();
        if (new_root == INVALID_PAGE_ID) return;
        SetKeyAt(new_root, 0, key);
        SetChildAt(new_root, 0, left_id);
        SetChildAt(new_root, 1, right_id);
        SetNumKeys(new_root, 1);
        root_page_id_ = new_root;

        BPlusPageHeader lh;
        Page *lp = bpm_->FetchPage(left_id);
        memcpy(&lh, lp->GetData(), sizeof(BPlusPageHeader));
        lh.parent_id = new_root;
        memcpy(lp->GetData(), &lh, sizeof(BPlusPageHeader));
        bpm_->UnpinPage(left_id, true);

        BPlusPageHeader rh;
        Page *rp = bpm_->FetchPage(right_id);
        memcpy(&rh, rp->GetData(), sizeof(BPlusPageHeader));
        rh.parent_id = new_root;
        memcpy(rp->GetData(), &rh, sizeof(BPlusPageHeader));
        bpm_->UnpinPage(right_id, true);
        return;
    }

    Page *rp = bpm_->FetchPage(right_id);
    BPlusPageHeader rh;
    memcpy(&rh, rp->GetData(), sizeof(BPlusPageHeader));
    rh.parent_id = parent_id;
    memcpy(rp->GetData(), &rh, sizeof(BPlusPageHeader));
    bpm_->UnpinPage(right_id, true);

    int nk = GetNumKeys(parent_id);
    int i = 0;
    while (i < nk && GetKeyAt(parent_id, i) < key) i++;

    for (int j = nk; j > i; j--) {
        SetKeyAt(parent_id, j, GetKeyAt(parent_id, j - 1));
        SetChildAt(parent_id, j + 1, GetChildAt(parent_id, j));
    }
    SetKeyAt(parent_id, i, key);
    SetChildAt(parent_id, i + 1, right_id);
    SetNumKeys(parent_id, nk + 1);
}

bool BPlusTree::GetValue(int64_t key, rid_t *value) {
    std::lock_guard<std::mutex> lock(latch_);
    if (root_page_id_ == INVALID_PAGE_ID) return false;

    page_id_t leaf_id = root_page_id_;
    while (!IsLeafPage(leaf_id)) {
        int nk = GetNumKeys(leaf_id);
        int i = 0;
        while (i < nk && GetKeyAt(leaf_id, i) <= key) i++;
        leaf_id = GetChildAt(leaf_id, i);
        if (leaf_id == INVALID_PAGE_ID) return false;
    }

    int nk = GetNumKeys(leaf_id);
    for (int i = 0; i < nk; i++) {
        if (GetKeyAt(leaf_id, i) == key) {
            *value = GetValueAt(leaf_id, i);
            return true;
        }
    }
    return false;
}

std::vector<rid_t> BPlusTree::RangeScan(int64_t lower, int64_t upper) {
    std::vector<rid_t> result;
    std::lock_guard<std::mutex> lock(latch_);
    if (root_page_id_ == INVALID_PAGE_ID) return result;

    page_id_t leaf_id = root_page_id_;
    while (!IsLeafPage(leaf_id)) {
        int nk = GetNumKeys(leaf_id);
        int i = 0;
        while (i < nk && GetKeyAt(leaf_id, i) < lower) i++;
        leaf_id = GetChildAt(leaf_id, i);
        if (leaf_id == INVALID_PAGE_ID) return result;
    }

    while (leaf_id != INVALID_PAGE_ID) {
        int nk = GetNumKeys(leaf_id);
        for (int i = 0; i < nk; i++) {
            int64_t k = GetKeyAt(leaf_id, i);
            if (k > upper) return result;
            if (k >= lower) {
                result.push_back(GetValueAt(leaf_id, i));
            }
        }
        leaf_id = GetNextPage(leaf_id);
    }
    return result;
}

bool BPlusTree::Remove(int64_t key) {
    std::lock_guard<std::mutex> lock(latch_);
    if (root_page_id_ == INVALID_PAGE_ID) return false;

    page_id_t leaf_id = root_page_id_;
    while (!IsLeafPage(leaf_id)) {
        int nk = GetNumKeys(leaf_id);
        int i = 0;
        while (i < nk && GetKeyAt(leaf_id, i) <= key) i++;
        leaf_id = GetChildAt(leaf_id, i);
        if (leaf_id == INVALID_PAGE_ID) return false;
    }

    int nk = GetNumKeys(leaf_id);
    int idx = -1;
    for (int i = 0; i < nk; i++) {
        if (GetKeyAt(leaf_id, i) == key) { idx = i; break; }
    }
    if (idx == -1) return false;

    for (int j = idx; j < nk - 1; j++) {
        SetKeyAt(leaf_id, j, GetKeyAt(leaf_id, j + 1));
        SetValueAt(leaf_id, j, GetValueAt(leaf_id, j + 1));
    }
    SetNumKeys(leaf_id, nk - 1);
    return true;
}

} // namespace minidb
