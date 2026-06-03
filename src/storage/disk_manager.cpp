#include "storage/disk_manager.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>

namespace minidb {

DiskManager::DiskManager(const std::string &db_file)
    : db_file_(db_file + DB_FILE_SUFFIX) {
    fd_ = open(db_file_.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("Cannot open database file: " + db_file_ + ", errno: " + std::to_string(errno));
    }
    off_t sz = lseek(fd_, 0, SEEK_END);
    num_pages_ = sz / PAGE_SIZE;
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

void DiskManager::ReadPage(page_id_t page_id, char *data) {
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    pread(fd_, data, PAGE_SIZE, offset);
}

void DiskManager::WritePage(page_id_t page_id, const char *data) {
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    pwrite(fd_, data, PAGE_SIZE, offset);
}

page_id_t DiskManager::AllocatePage() {
    return num_pages_++;
}

int DiskManager::GetNumPages() const {
    return num_pages_;
}

} // namespace minidb
