#include <iostream>
#include <cassert>
#include "index/b_plus_tree.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"

using namespace minidb;

void TestInsertAndScan() {
    std::cout << "=== B+Tree Insert & GetValue ===\n";
    DiskManager dm("test_btree");
    BufferPoolManager bpm(&dm);
    BPlusTree tree(&bpm);

    for (int i = 0; i < 200; i++) {
        assert(tree.Insert(i, i * 10));
    }
    std::cout << "  Inserted 200 keys.\n";

    rid_t value;
    for (int i = 0; i < 200; i++) {
        assert(tree.GetValue(i, &value));
        assert(value == i * 10);
    }
    std::cout << "  GetValue: PASSED\n";
}

void TestRangeScan() {
    std::cout << "=== B+Tree RangeScan ===\n";
    DiskManager dm("test_btree_range");
    BufferPoolManager bpm(&dm);
    BPlusTree tree(&bpm);

    for (int i = 0; i < 100; i++) {
        tree.Insert(i, i);
    }

    auto results = tree.RangeScan(10, 20);
    assert(results.size() == 11);
    for (int i = 0; i <= 10; i++) {
        assert(results[i] == i + 10);
    }
    std::cout << "  RangeScan: PASSED\n";
}

void TestRemove() {
    std::cout << "=== B+Tree Remove ===\n";
    DiskManager dm("test_btree_rm");
    BufferPoolManager bpm(&dm);
    BPlusTree tree(&bpm);

    for (int i = 0; i < 50; i++) {
        tree.Insert(i, i);
    }

    for (int i = 0; i < 25; i++) {
        assert(tree.Remove(i));
    }

    rid_t value;
    for (int i = 0; i < 25; i++) {
        assert(!tree.GetValue(i, &value));
    }
    for (int i = 25; i < 50; i++) {
        assert(tree.GetValue(i, &value));
        assert(value == i);
    }
    std::cout << "  Remove: PASSED\n";
}

int main() {
    TestInsertAndScan();
    TestRangeScan();
    TestRemove();
    std::cout << "\nAll B+Tree tests passed!\n";
    return 0;
}
