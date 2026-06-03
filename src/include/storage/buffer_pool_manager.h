#pragma once

#include <unordered_map>
#include <mutex>
#include "common/config.h"
#include "storage/page.h"
#include "storage/disk_manager.h"
#include "storage/lru_replacer.h"

namespace minidb {

class BufferPoolManager {
public:
    explicit BufferPoolManager(DiskManager *disk_manager);
    ~BufferPoolManager();

    Page *FetchPage(page_id_t page_id);
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    bool FlushPage(page_id_t page_id);
    Page *NewPage(page_id_t *page_id);
    bool DeletePage(page_id_t page_id);

private:
    bool FindVictim(frame_id_t *frame_id);
    void FlushAllPages();

    DiskManager *disk_manager_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    Page pages_[BUFFER_POOL_SIZE];
    LRUReplacer replacer_;
    std::mutex latch_;
    int num_pages_ = 0;
};

} // namespace minidb
