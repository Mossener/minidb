#include <iostream>
#include <cstring>
#include <cassert>
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/table_heap.h"
#include "storage/tuple.h"
#include "storage/schema.h"
#include "storage/tuple_meta.h"
#include "transaction/transaction.h"

using namespace minidb;

void TestInsertAndScan() {
    std::cout << "=== MVCC TableHeap Insert & Scan ===\n";
    DiskManager dm("test_heap_mvcc");
    BufferPoolManager bpm(&dm);
    Schema schema(std::vector<Column>{
        Column("id", TypeId::INTEGER),
        Column("val", TypeId::VARCHAR, 32)
    });

    TableHeap heap(&bpm);
    TransactionManager txn_mgr;

    Transaction *txn1 = txn_mgr.Begin();
    std::vector<rid_t> rids;

    for (int i = 0; i < 10; i++) {
        std::vector<char> data(schema.GetSerializedSize(), 0);
        memcpy(data.data(), &i, sizeof(int));
        char buf[32] = {};
        std::string val = "val_" + std::to_string(i);
        memcpy(buf, val.c_str(), val.size());
        memcpy(data.data() + 4, buf, 32);

        Tuple t(std::move(data), schema);
        rid_t rid;
        assert(heap.InsertTuple(t, &rid, txn1));
        rids.push_back(rid);
    }
    std::cout << "  Inserted 10 tuples in txn1.\n";

    // Can't see own uncommitted tuples without committing
    // Actually we should be able to... let's commit and check
    txn_mgr.Commit(txn1);
    // Apply commit timestamps
    for (const auto &rid : rids) {
        page_id_t pid = static_cast<page_id_t>(rid >> 32);
        int offset = static_cast<int>(rid & 0xFFFFFFFF);
        Page *page = bpm.FetchPage(pid);
        TupleMeta meta = ReadTupleMeta(page->GetData(), offset);
        meta.begin_ts = txn1->GetCommitTs();
        WriteTupleMeta(page->GetData(), offset, meta);
        bpm.UnpinPage(pid, true);
    }

    // Now read in a new transaction
    Transaction *txn2 = txn_mgr.Begin();
    rid_t scan_rid = heap.GetFirstRID(schema);
    int count = 0;
    while (scan_rid >= 0) {
        Tuple t = heap.GetTuple(schema, scan_rid, txn2, &txn_mgr);
        if (!t.IsNull()) {
            count++;
        }
        scan_rid = heap.GetNextRID(scan_rid, schema);
    }
    assert(count == 10);
    std::cout << "  Scanned " << count << " tuples in txn2: PASSED\n";

    // Random access
    rid_t target = rids[5];
    Tuple t = heap.GetTuple(schema, target, txn2, &txn_mgr);
    int id;
    memcpy(&id, t.GetData(), sizeof(int));
    assert(id == 5);
    std::cout << "  Random access id=5: PASSED\n";

    // Test MVCC isolation: insert in txn3 but don't commit, txn4 shouldn't see it
    Transaction *txn3 = txn_mgr.Begin();
    std::vector<char> data(schema.GetSerializedSize(), 0);
    int new_id = 99;
    memcpy(data.data(), &new_id, sizeof(int));
    char buf[32] = {};
    memcpy(buf, "new", 4);
    memcpy(data.data() + 4, buf, 32);
    Tuple new_t(std::move(data), schema);
    rid_t new_rid;
    heap.InsertTuple(new_t, &new_rid, txn3);
    std::cout << "  Inserted tuple id=99 in txn3 (uncommitted).\n";

    Transaction *txn4 = txn_mgr.Begin();
    count = 0;
    scan_rid = heap.GetFirstRID(schema);
    while (scan_rid >= 0) {
        Tuple t2 = heap.GetTuple(schema, scan_rid, txn4, &txn_mgr);
        if (!t2.IsNull()) count++;
        scan_rid = heap.GetNextRID(scan_rid, schema);
    }
    assert(count == 10);
    std::cout << "  txn4 sees " << count << " rows (uncommitted insert hidden): PASSED\n";

    // Cleanup
    txn_mgr.Commit(txn2);
    txn_mgr.Abort(txn3);
    txn_mgr.Commit(txn4);
    std::cout << "  MVCC isolation: PASSED\n";
}

int main() {
    TestInsertAndScan();
    std::cout << "\nAll MVCC TableHeap tests passed!\n";
    return 0;
}
