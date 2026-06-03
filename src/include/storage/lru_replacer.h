#pragma once

#include <list>
#include <unordered_map>
#include <mutex>
#include "common/config.h"

namespace minidb {

class LRUReplacer {
public:
    explicit LRUReplacer(size_t num_pages);
    ~LRUReplacer() = default;

    bool Victim(frame_id_t *frame_id);
    void Pin(frame_id_t frame_id);
    void Unpin(frame_id_t frame_id);
    size_t Size() const;

private:
    std::list<frame_id_t> lru_list_;
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> map_;
    mutable std::mutex latch_;
    size_t max_size_;
};

} // namespace minidb
