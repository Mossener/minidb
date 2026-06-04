#include "storage/log_manager.h"
#include <cstring>
#include <iostream>
#include <unordered_set>
#include <tuple>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace minidb {

// 日志固定头: lsn(8) + txn_id(8) + type(4) + data_size(4) = 24 字节
static constexpr size_t HEADER_SIZE = sizeof(int64_t) * 2 + sizeof(int32_t) * 2;

// 从 buffer 中解析日志头
static std::tuple<int64_t, int64_t, LogRecordType, size_t>
read_header(const char *p) {
    int64_t lsn, txn_id;
    int32_t type_val, data_size;
    memcpy(&lsn,       p,       8);
    memcpy(&txn_id,    p + 8,   8);
    memcpy(&type_val,  p + 16,  4);
    memcpy(&data_size, p + 20,  4);
    return {lsn, txn_id, static_cast<LogRecordType>(type_val),
            static_cast<size_t>(data_size)};
}

LogManager::LogManager(const std::string &path) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        std::cerr << "WAL: cannot open " << path << std::endl;
    }
}

LogManager::~LogManager() {
    flush_buffer();
    if (fd_ >= 0) ::close(fd_);
}

int64_t LogManager::Append(int64_t txn_id, LogRecordType type,
                           const char *data, int32_t data_size) {
    int64_t lsn = next_lsn_.fetch_add(1);

    char header[HEADER_SIZE];
    memcpy(header,      &lsn,       8);
    memcpy(header + 8,  &txn_id,    8);
    int32_t t = static_cast<int32_t>(type);
    memcpy(header + 16, &t,         4);
    memcpy(header + 20, &data_size, 4);

    size_t total = HEADER_SIZE + static_cast<size_t>(data_size);
    if (buf_offset_ + total > BUFFER_SIZE) {
        flush_buffer();
    }

    memcpy(buffer_ + buf_offset_, header, HEADER_SIZE);
    if (data_size > 0) {
        memcpy(buffer_ + buf_offset_ + HEADER_SIZE, data, data_size);
    }
    buf_offset_ += total;
    return lsn;
}

void LogManager::AppendCommit(int64_t txn_id) {
    Append(txn_id, LogRecordType::COMMIT, nullptr, 0);
    flush_buffer();  // WAL 关键: COMMIT 必须先 fsync，再写数据页
}

void LogManager::Flush() {
    flush_buffer();
}

void LogManager::flush_buffer() {
    if (buf_offset_ == 0 || fd_ < 0) return;
    ssize_t n = ::write(fd_, buffer_, buf_offset_);
    if (n < 0) {
        std::cerr << "WAL: write error" << std::endl;
    }
    ::fsync(fd_);  // 确保落盘
    buf_offset_ = 0;
}

std::vector<LogManager::RecoveryRecord> LogManager::Recover() {
    std::vector<RecoveryRecord> result;
    if (fd_ < 0) return result;

    struct stat st{};
    if (::fstat(fd_, &st) != 0) return result;
    if (st.st_size == 0) return result;

    std::vector<char> raw(static_cast<size_t>(st.st_size));
    ssize_t n = ::pread(fd_, raw.data(), raw.size(), 0);
    if (n < 0) return result;

    // 第一遍：找出所有已提交事务
    std::unordered_set<int64_t> committed;
    size_t pos = 0;
    while (pos + HEADER_SIZE <= raw.size()) {
        auto [lsn, txn_id, type, data_size] = read_header(raw.data() + pos);
        pos += HEADER_SIZE;
        if (pos + data_size > raw.size()) break;
        if (type == LogRecordType::COMMIT) {
            committed.insert(txn_id);
        }
        pos += data_size;
    }

    // 第二遍：收集已提交事务的 INSERT/DELETE
    pos = 0;
    while (pos + HEADER_SIZE <= raw.size()) {
        auto [lsn, txn_id, type, data_size] = read_header(raw.data() + pos);
        pos += HEADER_SIZE;
        if (pos + data_size > raw.size()) break;

        if ((type == LogRecordType::INSERT || type == LogRecordType::DELETE) &&
            committed.count(txn_id)) {
            RecoveryRecord rec;
            rec.txn_id = txn_id;
            rec.type = type;
            if (data_size > 0) {
                rec.data.assign(raw.data() + pos, raw.data() + pos + data_size);
            }
            result.push_back(std::move(rec));
        }
        pos += data_size;
    }
    return result;
}

} // namespace minidb
