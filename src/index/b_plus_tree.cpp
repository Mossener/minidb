#include "index/b_plus_tree.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <utility>

namespace minidb {

// ── Latch helpers (RAII) ──────────────────────────

BPlusTree::PageGuard BPlusTree::LockShared(page_id_t pid) {
    Page *p = bpm_->FetchPage(pid);
    if (p) p->GetLatch().RLock();
    return {p, pid, false, bpm_};
}

BPlusTree::PageGuard BPlusTree::LockExclusive(page_id_t pid) {
    Page *p = bpm_->FetchPage(pid);
    if (p) p->GetLatch().WLock();
    return {p, pid, true, bpm_};
}

// ── Constructor / Page creation ────────────────────

BPlusTree::BPlusTree(BufferPoolManager *bpm)
    : bpm_(bpm), root_page_id_(INVALID_PAGE_ID) {}

page_id_t BPlusTree::CreateLeafPage() {
    page_id_t pid;
    Page *page = bpm_->NewPage(&pid);
    if (!page) return INVALID_PAGE_ID;
    BPlusPageHeader h{true, 0, INVALID_PAGE_ID, INVALID_PAGE_ID};
    memcpy(page->GetData(), &h, sizeof(h));
    memset(page->GetData() + sizeof(h), 0, PAGE_SIZE - sizeof(h));
    bpm_->UnpinPage(pid, true);
    return pid;
}

page_id_t BPlusTree::CreateInternalPage() {
    page_id_t pid;
    Page *page = bpm_->NewPage(&pid);
    if (!page) return INVALID_PAGE_ID;
    BPlusPageHeader h{false, 0, INVALID_PAGE_ID, INVALID_PAGE_ID};
    memcpy(page->GetData(), &h, sizeof(h));
    memset(page->GetData() + sizeof(h), 0, PAGE_SIZE - sizeof(h));
    bpm_->UnpinPage(pid, true);
    return pid;
}

int BPlusTree::GetNumKeys(page_id_t page_id) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusPageHeader h;
    memcpy(&h, page->GetData(), sizeof(h));
    bpm_->UnpinPage(page_id, false);
    return h.num_keys;
}

// ── Key/Value/Child access ─────────────────────────

int64_t BPlusTree::GetKeyAt(page_id_t page_id, int idx) {
    Page *page = bpm_->FetchPage(page_id);
    int64_t k;
    memcpy(&k, page->GetData() + sizeof(BPlusPageHeader) + idx * 8, 8);
    bpm_->UnpinPage(page_id, false);
    return k;
}

void BPlusTree::SetKeyAt(page_id_t page_id, int idx, int64_t key) {
    Page *page = bpm_->FetchPage(page_id);
    memcpy(page->GetData() + sizeof(BPlusPageHeader) + idx * 8, &key, 8);
    bpm_->UnpinPage(page_id, true);
}

rid_t BPlusTree::GetValueAt(page_id_t page_id, int idx) {
    int off = MAX_KEYS * 8;
    Page *page = bpm_->FetchPage(page_id);
    rid_t v;
    memcpy(&v, page->GetData() + sizeof(BPlusPageHeader) + off + idx * 8, 8);
    bpm_->UnpinPage(page_id, false);
    return v;
}

void BPlusTree::SetValueAt(page_id_t page_id, int idx, rid_t val) {
    int off = MAX_KEYS * 8;
    Page *page = bpm_->FetchPage(page_id);
    memcpy(page->GetData() + sizeof(BPlusPageHeader) + off + idx * 8, &val, 8);
    bpm_->UnpinPage(page_id, true);
}

page_id_t BPlusTree::GetChildAt(page_id_t page_id, int idx) {
    int off = MAX_KEYS * 8 + MAX_KEYS * 8;
    Page *page = bpm_->FetchPage(page_id);
    page_id_t c;
    memcpy(&c, page->GetData() + sizeof(BPlusPageHeader) + off + idx * 4, 4);
    bpm_->UnpinPage(page_id, false);
    return c;
}

void BPlusTree::SetChildAt(page_id_t page_id, int idx, page_id_t child) {
    int off = MAX_KEYS * 8 + MAX_KEYS * 8;
    Page *page = bpm_->FetchPage(page_id);
    memcpy(page->GetData() + sizeof(BPlusPageHeader) + off + idx * 4, &child, 4);
    BPlusPageHeader h;
    memcpy(&h, page->GetData(), sizeof(h));
    h.num_keys = std::max(h.num_keys, idx);
    memcpy(page->GetData(), &h, sizeof(h));
    bpm_->UnpinPage(page_id, true);
}

page_id_t BPlusTree::GetNextPage(page_id_t page_id) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusPageHeader h;
    memcpy(&h, page->GetData(), sizeof(h));
    bpm_->UnpinPage(page_id, false);
    return h.next_id;
}

void BPlusTree::SetNextPage(page_id_t page_id, page_id_t next) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusPageHeader h;
    memcpy(&h, page->GetData(), sizeof(h));
    h.next_id = next;
    memcpy(page->GetData(), &h, sizeof(h));
    bpm_->UnpinPage(page_id, true);
}

// ── Insert (抄自 PG _bt_search + _bt_split 协议) ─────

// ── B-link helper: follow right-links when key > last_key ──

bool BPlusTree::Insert(int64_t key, rid_t value) {
    if (root_page_id_ == INVALID_PAGE_ID) {
        page_id_t pid = CreateLeafPage();
        if (pid == INVALID_PAGE_ID) return false;
        root_page_id_ = pid;
        SetKeyAt(pid, 0, key);
        SetValueAt(pid, 0, value);
        BPlusPageHeader h;
        Page *p = bpm_->FetchPage(pid);
        memcpy(&h, p->GetData(), sizeof(h));
        h.num_keys = 1;
        memcpy(p->GetData(), &h, sizeof(h));
        bpm_->UnpinPage(pid, true);
        return true;
    }

    // 步骤 1: RLock 下降，记录路径栈
    std::vector<page_id_t> path;
    auto cur = LockShared(root_page_id_);
    path.push_back(cur.pid);

    while (true) {
        BPlusPageHeader h;
        memcpy(&h, cur.page->GetData(), sizeof(h));
        if (h.is_leaf) break;

        // B-link: key > 所有键 → 沿 right-link 右移
        if (h.num_keys > 0 && key > GetKeyAt(cur.pid, h.num_keys - 1)) {
            page_id_t right = GetNextPage(cur.pid);
            if (right != INVALID_PAGE_ID) {
                cur.release();
                cur = LockShared(right);
                path.back() = right;
                continue;
            }
        }

        int idx = 0;
        while (idx < h.num_keys && GetKeyAt(cur.pid, idx) <= key) idx++;
        page_id_t child_id = GetChildAt(cur.pid, idx);
        if (child_id == INVALID_PAGE_ID) { cur.release(); return false; }

        auto child = LockShared(child_id);
        cur.release();
        cur = std::move(child);
        path.push_back(child_id);
    }
    page_id_t leaf_pid = cur.pid;
    cur.release();

    // 步骤 2: WLock leaf, 尝试乐观插入
    auto leaf = LockExclusive(leaf_pid);
    BPlusPageHeader lh;
    memcpy(&lh, leaf.page->GetData(), sizeof(lh));

    if (lh.num_keys < MAX_KEYS) {
        InsertIntoLeaf(leaf_pid, key, value);
        leaf.release();
        return true;
    }

    // 步骤 3: 叶子满 — WLock 祖先
    std::vector<PageGuard> ancestors;
    for (int i = (int)path.size() - 2; i >= 0; i--) {
        ancestors.push_back(LockExclusive(path[i]));
    }

    // 步骤 4: 分裂
    SplitLeaf(leaf_pid);
    leaf.release();

    // 步骤 5: 释放祖先锁（必须在递归 Insert 前释放，否则 LockShared 重入死锁）
    ancestors.clear();

    return Insert(key, value);
}

// ── Remove ─────────────────────────────────────────

bool BPlusTree::Remove(int64_t key) {
    if (root_page_id_ == INVALID_PAGE_ID) return false;

    page_id_t leaf_id = root_page_id_;
    while (true) {
        BPlusPageHeader h;
        Page *p = bpm_->FetchPage(leaf_id);
        memcpy(&h, p->GetData(), sizeof(h));
        bpm_->UnpinPage(leaf_id, false);
        if (h.is_leaf) break;

        int idx = 0;
        while (idx < h.num_keys && GetKeyAt(leaf_id, idx) <= key) idx++;
        leaf_id = GetChildAt(leaf_id, idx);
        if (leaf_id == INVALID_PAGE_ID) return false;
    }

    return RemoveFromLeaf(leaf_id, key);
}

// ── GetValue with RLock coupling ───────────────────

bool BPlusTree::GetValue(int64_t key, rid_t *value) {
    if (root_page_id_ == INVALID_PAGE_ID) return false;

    auto cur = LockShared(root_page_id_);

    while (true) {
        BPlusPageHeader h;
        memcpy(&h, cur.page->GetData(), sizeof(h));
        if (h.is_leaf) break;

        // 直接读 page data，不调 FetchPage（已 RLock）
        const char *data = cur.page->GetData() + sizeof(BPlusPageHeader);
        int idx = 0;
        int64_t k;
        while (idx < h.num_keys) {
            memcpy(&k, data + idx * 8, 8);
            if (k > key) break;
            idx++;
        }
        page_id_t child_id;
        int child_off = MAX_KEYS * 8 + MAX_KEYS * 8;
        memcpy(&child_id, data + child_off + idx * 4, 4);
        if (child_id == INVALID_PAGE_ID) { cur.release(); return false; }

        auto child = LockShared(child_id);
        cur.release();
        cur = std::move(child);
    }

    // 在叶子上直接读 key 和 value
    const char *leaf_data = cur.page->GetData() + sizeof(BPlusPageHeader);
    BPlusPageHeader lh;
    memcpy(&lh, cur.page->GetData(), sizeof(lh));
    int val_off = MAX_KEYS * 8;
    for (int i = 0; i < lh.num_keys; i++) {
        int64_t k;
        memcpy(&k, leaf_data + i * 8, 8);
        if (k == key) {
            memcpy(value, leaf_data + val_off + i * 8, 8);
            cur.release();
            return true;
        }
    }
    cur.release();
    return false;
}

// ── RangeScan with RLock coupling ──────────────────

std::vector<rid_t> BPlusTree::RangeScan(int64_t lower, int64_t upper) {
    std::vector<rid_t> result;
    if (root_page_id_ == INVALID_PAGE_ID) return result;

    auto cur = LockShared(root_page_id_);
    page_id_t child_id;

    while (true) {
        BPlusPageHeader h;
        memcpy(&h, cur.page->GetData(), sizeof(h));
        if (h.is_leaf) break;

        int idx = 0;
        while (idx < h.num_keys && GetKeyAt(cur.pid, idx) < lower) idx++;
        child_id = GetChildAt(cur.pid, idx);
        if (child_id == INVALID_PAGE_ID) { cur.release(); return result; }

        auto child = LockShared(child_id);
        cur.release();
        cur = std::move(child);
    }

    page_id_t leaf_id = cur.pid;
    cur.release();

    while (leaf_id != INVALID_PAGE_ID) {
        auto lp = LockShared(leaf_id);
        BPlusPageHeader h;
        memcpy(&h, lp.page->GetData(), sizeof(h));
        for (int i = 0; i < h.num_keys; i++) {
            int64_t k = GetKeyAt(leaf_id, i);
            if (k > upper) { lp.release(); return result; }
            if (k >= lower) result.push_back(GetValueAt(leaf_id, i));
        }
        page_id_t next = GetNextPage(leaf_id);
        lp.release();
        leaf_id = next;
    }
    return result;
}

// ── Insert helpers ─────────────────────────────────

void BPlusTree::InsertIntoLeaf(page_id_t leaf_id, int64_t key, rid_t value) {
    Page *page = bpm_->FetchPage(leaf_id);
    BPlusPageHeader h;
    memcpy(&h, page->GetData(), sizeof(h));
    bpm_->UnpinPage(leaf_id, false);

    int nk = h.num_keys, i = 0;
    while (i < nk && GetKeyAt(leaf_id, i) < key) i++;
    if (i < nk && GetKeyAt(leaf_id, i) == key) return;

    for (int j = nk; j > i; j--) {
        SetKeyAt(leaf_id, j, GetKeyAt(leaf_id, j - 1));
        SetValueAt(leaf_id, j, GetValueAt(leaf_id, j - 1));
    }
    SetKeyAt(leaf_id, i, key);
    SetValueAt(leaf_id, i, value);

    Page *p2 = bpm_->FetchPage(leaf_id);
    memcpy(&h, p2->GetData(), sizeof(h));
    h.num_keys = nk + 1;
    memcpy(p2->GetData(), &h, sizeof(h));
    bpm_->UnpinPage(leaf_id, true);
}

bool BPlusTree::RemoveFromLeaf(page_id_t leaf_id, int64_t key) {
    Page *page = bpm_->FetchPage(leaf_id);
    BPlusPageHeader h;
    memcpy(&h, page->GetData(), sizeof(h));
    bpm_->UnpinPage(leaf_id, false);

    int nk = h.num_keys, idx = -1;
    for (int i = 0; i < nk; i++)
        if (GetKeyAt(leaf_id, i) == key) { idx = i; break; }
    if (idx == -1) return false;

    for (int j = idx; j < nk - 1; j++) {
        SetKeyAt(leaf_id, j, GetKeyAt(leaf_id, j + 1));
        SetValueAt(leaf_id, j, GetValueAt(leaf_id, j + 1));
    }
    Page *p2 = bpm_->FetchPage(leaf_id);
    h.num_keys = nk - 1;
    memcpy(p2->GetData(), &h, sizeof(h));
    bpm_->UnpinPage(leaf_id, true);
    return true;
}

// ── Split / InsertIntoParent ───────────────────────

void BPlusTree::SplitLeaf(page_id_t leaf_id) {
    Page *page = bpm_->FetchPage(leaf_id);
    BPlusPageHeader h;
    memcpy(&h, page->GetData(), sizeof(h));
    bpm_->UnpinPage(leaf_id, false);

    page_id_t new_id = CreateLeafPage();
    if (new_id == INVALID_PAGE_ID) return;

    int total = h.num_keys, split = total / 2;
    for (int i = split; i < total; i++) {
        SetKeyAt(new_id, i - split, GetKeyAt(leaf_id, i));
        SetValueAt(new_id, i - split, GetValueAt(leaf_id, i));
    }
    Page *p1 = bpm_->FetchPage(leaf_id);
    h.num_keys = split;
    memcpy(p1->GetData(), &h, sizeof(h));
    bpm_->UnpinPage(leaf_id, true);

    Page *p2 = bpm_->FetchPage(new_id);
    BPlusPageHeader nh{true, total - split, INVALID_PAGE_ID, GetNextPage(leaf_id)};
    memcpy(p2->GetData(), &nh, sizeof(nh));
    bpm_->UnpinPage(new_id, true);

    SetNextPage(leaf_id, new_id);
    InsertIntoParent(leaf_id, GetKeyAt(new_id, 0), new_id);
}

void BPlusTree::SplitInternal(page_id_t internal_id) {
    Page *page = bpm_->FetchPage(internal_id);
    BPlusPageHeader h;
    memcpy(&h, page->GetData(), sizeof(h));
    bpm_->UnpinPage(internal_id, false);

    page_id_t new_id = CreateInternalPage();
    if (new_id == INVALID_PAGE_ID) return;

    int total = h.num_keys, split = total / 2;
    int64_t up_key = GetKeyAt(internal_id, split);
    for (int i = split + 1; i < total; i++) {
        SetKeyAt(new_id, i - split - 1, GetKeyAt(internal_id, i));
        SetChildAt(new_id, i - split - 1, GetChildAt(internal_id, i));
    }
    SetChildAt(new_id, total - split - 1, GetChildAt(internal_id, total));

    Page *p1 = bpm_->FetchPage(internal_id);
    h.num_keys = split;
    memcpy(p1->GetData(), &h, sizeof(h));
    bpm_->UnpinPage(internal_id, true);

    Page *p2 = bpm_->FetchPage(new_id);
    BPlusPageHeader nh{false, total - split - 1, INVALID_PAGE_ID, INVALID_PAGE_ID};
    memcpy(p2->GetData(), &nh, sizeof(nh));
    bpm_->UnpinPage(new_id, true);

    // B-link: 旧 node 的 right-link 指向新 sibling
    SetNextPage(internal_id, new_id);

    InsertIntoParent(internal_id, up_key, new_id);
}

void BPlusTree::InsertIntoParent(page_id_t left_id, int64_t key, page_id_t right_id) {
    Page *lp = bpm_->FetchPage(left_id);
    BPlusPageHeader lh;
    memcpy(&lh, lp->GetData(), sizeof(lh));
    bpm_->UnpinPage(left_id, false);

    page_id_t parent_id = lh.parent_id;
    if (parent_id == INVALID_PAGE_ID) {
        page_id_t nr = CreateInternalPage();
        SetKeyAt(nr, 0, key);
        SetChildAt(nr, 0, left_id);
        SetChildAt(nr, 1, right_id);
        Page *rp = bpm_->FetchPage(nr);
        BPlusPageHeader nh{false, 1, INVALID_PAGE_ID, INVALID_PAGE_ID};
        memcpy(rp->GetData(), &nh, sizeof(nh));
        bpm_->UnpinPage(nr, true);
        root_page_id_ = nr;

        Page *pl = bpm_->FetchPage(left_id);
        lh.parent_id = nr;
        memcpy(pl->GetData(), &lh, sizeof(lh));
        bpm_->UnpinPage(left_id, true);

        Page *pr = bpm_->FetchPage(right_id);
        BPlusPageHeader rh;
        memcpy(&rh, pr->GetData(), sizeof(rh));
        rh.parent_id = nr;
        memcpy(pr->GetData(), &rh, sizeof(rh));
        bpm_->UnpinPage(right_id, true);
        return;
    }

    Page *rp = bpm_->FetchPage(right_id);
    BPlusPageHeader rh;
    memcpy(&rh, rp->GetData(), sizeof(rh));
    rh.parent_id = parent_id;
    memcpy(rp->GetData(), &rh, sizeof(rh));
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
    Page *pp = bpm_->FetchPage(parent_id);
    BPlusPageHeader ph;
    memcpy(&ph, pp->GetData(), sizeof(ph));
    ph.num_keys = nk + 1;
    memcpy(pp->GetData(), &ph, sizeof(ph));
    bpm_->UnpinPage(parent_id, true);
}

} // namespace minidb
