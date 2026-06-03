// 页面模块 - 定义内存中数据页的结构，是缓冲池管理的基本单元
// 每个页面大小固定为 PAGE_SIZE(4096字节)，包含页面ID、脏标记和引用计数

#pragma once

#include <cstring>
#include "common/config.h"

namespace minidb {

// 数据页: 缓冲池中缓存的基本单元，大小为固定 4KB
class Page {
public:
    Page() { Reset(); }

    // 获取页面数据区指针 (可读写/只读)
    inline char *GetData() { return data_; }
    inline const char *GetData() const { return data_; }
    // 页面ID: 唯一标识磁盘上对应的页面
    inline page_id_t GetPageId() const { return page_id_; }
    inline void SetPageId(page_id_t id) { page_id_ = id; }
    // 脏标记: 页面数据被修改后设为 true，表示需要写回磁盘
    inline bool IsDirty() const { return is_dirty_; }
    inline void SetDirty(bool dirty) { is_dirty_ = dirty; }
    // 引用计数: 记录当前有多少组件正在使用该页面
    inline int GetPinCount() const { return pin_count_; }
    // 增加引用计数 (获取页面时调用)
    inline void Pin() { pin_count_++; }
    // 减少引用计数 (释放页面时调用，但不能小于0)
    inline void Unpin() {
        if (pin_count_ > 0) pin_count_--;
    }

    // 重置页面状态: 清零数据、取消关联的页面ID、清除脏标记和引用计数
    void Reset() {
        memset(data_, 0, PAGE_SIZE);
        page_id_ = INVALID_PAGE_ID;
        is_dirty_ = false;
        pin_count_ = 0;
    }

private:
    char data_[PAGE_SIZE];                   // 页面实际数据区 (4KB)
    page_id_t page_id_ = INVALID_PAGE_ID;    // 关联的磁盘页面ID
    bool is_dirty_ = false;                  // 脏标记 (是否被修改)
    int pin_count_ = 0;                      // 引用计数 (pinned 时不可淘汰)
};

} // namespace minidb
