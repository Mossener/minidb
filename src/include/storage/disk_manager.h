#pragma once

#include <cstring>
#include <string>
#include "common/config.h"

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string &db_file);
    ~DiskManager();

    void ReadPage(page_id_t page_id, char *data);
    void WritePage(page_id_t page_id, const char *data);
    page_id_t AllocatePage();
    int GetNumPages() const;

private:
    std::string db_file_;
    int fd_;
    int num_pages_ = 0;
    static constexpr const char *DB_FILE_SUFFIX = ".minidb";
};

} // namespace minidb
