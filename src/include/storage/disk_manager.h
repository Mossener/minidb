// 磁盘管理器模块 - 负责数据库文件的读写操作
// 将页面以固定大小顺序写入磁盘文件，提供页面级随机访问能力

#pragma once

#include <cstring>
#include <string>
#include "common/config.h"

namespace minidb {

class DiskManager {
public:
    // 构造函数: 打开或创建指定名称的数据库文件 (自动追加 .minidb 后缀)
    explicit DiskManager(const std::string &db_file);
    ~DiskManager();

    // 从磁盘读取指定页面到 data 缓冲区
    void ReadPage(page_id_t page_id, char *data);
    // 将 data 缓冲区内容写入磁盘的指定页面
    void WritePage(page_id_t page_id, const char *data);
    // 分配新页面: 扩展文件大小，返回新页面ID
    page_id_t AllocatePage();
    // 返回当前文件中的页面总数
    int GetNumPages() const;

private:
    std::string db_file_;                // 数据库文件路径
    int fd_;                             // 文件描述符
    int num_pages_ = 0;                  // 当前页面总数
    static constexpr const char *DB_FILE_SUFFIX = ".minidb";  // 文件后缀
};

} // namespace minidb
