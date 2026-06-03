#pragma once

#include <cstring>
#include "common/config.h"

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

private:
    char data_[PAGE_SIZE];
    page_id_t page_id_ = INVALID_PAGE_ID;
    bool is_dirty_ = false;
    int pin_count_ = 0;
};

} // namespace minidb
