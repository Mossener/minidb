// 页面模块 - 定义内存中数据页的结构，是缓冲池管理的基本单元
// 每个页面大小固定为 PAGE_SIZE(4096字节)，包含页面ID、脏标记和引用计数

#pragma once

#include <cstring>
#include "common/config.h"
#include "common/rwlatch.h"

namespace minidb {

class Page {
public:
    Page() { Reset(); }

    inline char *GetData() { return data_; }
    inline const char *GetData() const { return data_; }
    inline page_id_t GetPageId() const { return page_id_; }
    inline void SetPageId(page_id_t id) { page_id_ = id; }
    inline bool IsDirty() const { return is_dirty_; }
    inline void SetDirty(bool dirty) { is_dirty_ = dirty; }
    inline int GetPinCount() const { return pin_count_; }
    inline void Pin() { pin_count_++; }
    inline void Unpin() {
        if (pin_count_ > 0) pin_count_--;
    }

    void Reset() {
        memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
        is_dirty_ = false;
        pin_count_ = 0;
    }

    // Per-page latch for B+Tree Latch Crabbing
    ReaderWriterLatch &GetLatch() { return latch_; }

private:
    char data_[PAGE_SIZE];
    page_id_t page_id_ = INVALID_PAGE_ID;
    bool is_dirty_ = false;
    int pin_count_ = 0;
    ReaderWriterLatch latch_;
};

} // namespace minidb
