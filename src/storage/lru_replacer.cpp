#include "storage/lru_replacer.h"

namespace minidb {

LRUReplacer::LRUReplacer(size_t num_pages) : max_size_(num_pages) {}

bool LRUReplacer::Victim(frame_id_t *frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (lru_list_.empty()) return false;
    *frame_id = lru_list_.back();
    lru_list_.pop_back();
    map_.erase(*frame_id);
    return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto it = map_.find(frame_id);
    if (it != map_.end()) {
        lru_list_.erase(it->second);
        map_.erase(it);
    }
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (map_.find(frame_id) != map_.end()) return;
    if (lru_list_.size() >= max_size_) {
        lru_list_.pop_back();
        map_.erase(lru_list_.back());
    }
    lru_list_.push_front(frame_id);
    map_[frame_id] = lru_list_.begin();
}

size_t LRUReplacer::Size() const {
    std::lock_guard<std::mutex> lock(latch_);
    return lru_list_.size();
}

} // namespace minidb
