#pragma once

#include <mutex>
#include <condition_variable>
#include <shared_mutex>

namespace minidb {

class ReaderWriterLatch {
public:
    ReaderWriterLatch() = default;
    ~ReaderWriterLatch() = default;

    void WLock() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (writer_entered_) {
            cv_.wait(lock);
        }
        writer_entered_ = true;
        while (readers_ > 0) {
            cv_.wait(lock);
        }
    }

    void WUnlock() {
        std::unique_lock<std::mutex> lock(mutex_);
        writer_entered_ = false;
        cv_.notify_all();
    }

    void RLock() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (writer_entered_) {
            cv_.wait(lock);
        }
        readers_++;
    }

    void RUnlock() {
        std::unique_lock<std::mutex> lock(mutex_);
        readers_--;
        if (readers_ == 0) {
            cv_.notify_all();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int readers_ = 0;
    bool writer_entered_ = false;
};

} // namespace minidb
