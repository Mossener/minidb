#include "storage/buffer_pool_manager.h"
#include <cassert>

namespace minidb {

BufferPoolManager::BufferPoolManager(DiskManager *disk_manager)
    : disk_manager_(disk_manager), replacer_(BUFFER_POOL_SIZE) {
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        replacer_.Unpin(i);
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        Page *page = &pages_[it->second];
        page->Pin();
        replacer_.Pin(it->second);
        return page;
    }
    frame_id_t frame_id;
    if (!FindVictim(&frame_id)) {
        return nullptr;
    }
    Page *page = &pages_[frame_id];
    if (page->GetPageId() != INVALID_PAGE_ID) {
        page_table_.erase(page->GetPageId());
    }
    disk_manager_->ReadPage(page_id, page->GetData());
    page->SetPageId(page_id);
    page->SetDirty(false);
    page->Pin();
    replacer_.Pin(frame_id);
    page_table_[page_id] = frame_id;
    return page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    Page *page = &pages_[it->second];
    if (is_dirty) page->SetDirty(true);
    page->Unpin();
    if (page->GetPinCount() == 0) {
        replacer_.Unpin(it->second);
    }
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    Page *page = &pages_[it->second];
    if (page->IsDirty()) {
        disk_manager_->WritePage(page_id, page->GetData());
        page->SetDirty(false);
    }
    return true;
}

Page *BufferPoolManager::NewPage(page_id_t *page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    frame_id_t frame_id;
    if (!FindVictim(&frame_id)) {
        return nullptr;
    }
    Page *page = &pages_[frame_id];
    if (page->GetPageId() != INVALID_PAGE_ID) {
        page_table_.erase(page->GetPageId());
    }
    *page_id = disk_manager_->AllocatePage();
    page->Reset();
    page->SetPageId(*page_id);
    page->Pin();
    replacer_.Pin(frame_id);
    page_table_[*page_id] = frame_id;
    return page;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    Page *page = &pages_[it->second];
    if (page->GetPinCount() > 0) return false;
    if (page->IsDirty()) {
        disk_manager_->WritePage(page_id, page->GetData());
    }
    page_table_.erase(it);
    page->Reset();
    replacer_.Pin(it->second);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (pages_[i].GetPageId() != INVALID_PAGE_ID) {
            if (pages_[i].IsDirty()) {
                disk_manager_->WritePage(pages_[i].GetPageId(), pages_[i].GetData());
                pages_[i].SetDirty(false);
            }
        }
    }
}

bool BufferPoolManager::FindVictim(frame_id_t *frame_id) {
    if (!replacer_.Victim(frame_id)) {
        return false;
    }
    Page *page = &pages_[*frame_id];
    if (page->GetPinCount() > 0) {
        return false;
    }
    if (page->IsDirty() && page->GetPageId() != INVALID_PAGE_ID) {
        disk_manager_->WritePage(page->GetPageId(), page->GetData());
        page->SetDirty(false);
    }
    return true;
}

} // namespace minidb
