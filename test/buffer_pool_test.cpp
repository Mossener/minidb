#include <iostream>
#include <cstring>
#include <cassert>
#include <unistd.h>
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/page.h"
#include "storage/tuple_meta.h"

using namespace minidb;

void TestDiskManager() {
    std::cout << "=== DiskManager Test ===\n";
    unlink("test_disk.minidb");
    DiskManager dm("test_disk");
    assert(dm.GetNumPages() == 0);

    char write_buf[PAGE_SIZE];
    memset(write_buf, 'A', PAGE_SIZE);
    page_id_t pid = dm.AllocatePage();
    assert(pid == 0);
    dm.WritePage(pid, write_buf);

    char read_buf[PAGE_SIZE];
    dm.ReadPage(pid, read_buf);
    assert(memcmp(write_buf, read_buf, PAGE_SIZE) == 0);
    std::cout << "  DiskManager: PASSED\n";
}

void TestLRUReplacer() {
    std::cout << "=== LRUReplacer Test ===\n";
    LRUReplacer replacer(4);

    frame_id_t frame;
    assert(!replacer.Victim(&frame));

    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    assert(replacer.Size() == 3);

    assert(replacer.Victim(&frame));
    assert(frame == 1 || frame == 2 || frame == 3);

    std::cout << "  LRUReplacer: PASSED\n";
}

void TestBufferPool() {
    std::cout << "=== BufferPool Test ===\n";
    DiskManager dm("test_bp");
    BufferPoolManager bpm(&dm);

    page_id_t pid1, pid2;
    Page *p1 = bpm.NewPage(&pid1);
    assert(p1 != nullptr);
    assert(p1->GetPinCount() == 1);

    Page *p2 = bpm.NewPage(&pid2);
    assert(p2 != nullptr);

    memcpy(p1->GetData(), "Hello Buffer Pool", 18);
    bpm.UnpinPage(pid1, true);
    bpm.UnpinPage(pid2, false);

    Page *pf = bpm.FetchPage(pid1);
    assert(pf != nullptr);
    assert(memcmp(pf->GetData(), "Hello Buffer Pool", 18) == 0);
    bpm.UnpinPage(pid1, false);

    bpm.DeletePage(pid1);
    Page *p_reload = bpm.FetchPage(pid1);
    assert(p_reload != nullptr);
    assert(memcmp(p_reload->GetData(), "Hello Buffer Pool", 18) == 0);
    bpm.UnpinPage(pid1, false);

    std::cout << "  BufferPool: PASSED\n";
}

int main() {
    TestDiskManager();
    TestLRUReplacer();
    TestBufferPool();
    std::cout << "\nAll Buffer Pool tests passed!\n";
    return 0;
}
