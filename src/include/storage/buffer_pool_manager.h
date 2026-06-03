// 缓冲池管理模块 - 在内存中缓存磁盘页面，使用 LRU 淘汰策略
// 作为页面访问的统一入口，提供 Fix/Unfix 语义管理页面的生命周期

#pragma once

#include <unordered_map>
#include <mutex>
#include "common/config.h"
#include "storage/page.h"
#include "storage/disk_manager.h"
#include "storage/lru_replacer.h"

namespace minidb {

// 缓冲池管理器: 管理固定大小的页面数组，通过 page_table 映射 page_id -> frame_id
class BufferPoolManager {
public:
    explicit BufferPoolManager(DiskManager *disk_manager);
    ~BufferPoolManager();

    // 获取页面: 如果已在缓冲池则直接返回，否则从磁盘读取
    Page *FetchPage(page_id_t page_id);
    // 释放页面: 减少引用计数，标记是否为脏页
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    // 将指定页面写回磁盘
    bool FlushPage(page_id_t page_id);
    // 分配新页面: 在磁盘上分配空间，在缓冲池中创建对应页面
    Page *NewPage(page_id_t *page_id);
    // 删除页面: 从缓冲池和磁盘中移除
    bool DeletePage(page_id_t page_id);

private:
    // 通过 LRU 替换器找到一个可淘汰的帧
    bool FindVictim(frame_id_t *frame_id);
    // 将缓冲池中所有脏页写回磁盘 (析构时调用)
    void FlushAllPages();

    DiskManager *disk_manager_;                              // 磁盘管理器，负责实际I/O
    std::unordered_map<page_id_t, frame_id_t> page_table_;   // 页面ID -> 帧索引的映射表
    Page pages_[BUFFER_POOL_SIZE];                           // 缓冲池页面数组 (固定大小128)
    LRUReplacer replacer_;                                    // LRU 替换器，决定淘汰哪个页面
    std::mutex latch_;                                        // 互斥锁，保证线程安全
    int num_pages_ = 0;                                       // 当前缓冲池中已使用的页面数
};

} // namespace minidb
