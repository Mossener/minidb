#pragma once

#include <string>
#include <atomic>
#include <vector>
#include "common/config.h"

namespace minidb {

// 日志记录类型
enum class LogRecordType : int32_t {
    BEGIN  = 0,
    COMMIT = 1,
    ABORT  = 2,
    INSERT = 3,
    DELETE = 4,
};

// 磁盘日志记录（固定头 24 字节 + 变长数据）
// 布局: [lsn: int64][txn_id: int64][type: int32][data_size: int32][data...]
class LogManager {
public:
    explicit LogManager(const std::string &log_path);
    ~LogManager();

    // 追加一条日志（写入内存 buffer，非立即刷盘）
    int64_t Append(int64_t txn_id, LogRecordType type,
                   const char *data, int32_t data_size);

    // 提交事务：先刷 COMMIT 日志到磁盘，再修改数据页
    void AppendCommit(int64_t txn_id);

    // 强制刷盘（fsync）
    void Flush();

    // === 恢复（崩溃恢复时调用）===
    // 返回所有已提交事务的日志记录，由调用方重放到数据页
    struct RecoveryRecord {
        int64_t txn_id;
        LogRecordType type;
        std::vector<char> data;  // INSERT: tuple page data | DELETE: [rid][old_end_ts]
    };
    std::vector<RecoveryRecord> Recover();

    int64_t GetNextLSN() { return next_lsn_.fetch_add(1); }

private:
    void flush_buffer();  // 写磁盘 + fsync

    int fd_ = -1;
    std::atomic<int64_t> next_lsn_{1};

    static constexpr size_t BUFFER_SIZE = 4096;
    char buffer_[BUFFER_SIZE];
    size_t buf_offset_ = 0;
};

} // namespace minidb
