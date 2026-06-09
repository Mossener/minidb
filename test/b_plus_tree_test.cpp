#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include "index/b_plus_tree.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"

using namespace minidb;
using namespace std::chrono;
using namespace std::chrono;

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

void BenchPointQuery() {
    DiskManager dm("bench_pq");
    BufferPoolManager bpm(&dm);
    BPlusTree tree(&bpm);
    int N = 5000;
    for (int i = 0; i < N; i++) tree.Insert(i, i * 10);
    rid_t v;
    int M = 5000;
    auto t0 = high_resolution_clock::now();
    for (int i = 0; i < M; i++) tree.GetValue(i % N, &v);
    auto t1 = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(t1 - t0).count();
    std::cout << "Single-thread read: " << (us*1000.0/M) << " ns/op (" << N << " keys, " << M << " ops)\n";
}

void BenchInsert() {
    DiskManager dm("bench_ins");
    BufferPoolManager bpm(&dm);
    BPlusTree tree(&bpm);
    int N = 5000;
    auto t0 = high_resolution_clock::now();
    for (int i = 0; i < N; i++) tree.Insert(i, i * 10);
    auto t1 = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(t1 - t0).count();
    std::cout << "Single-thread insert: " << (us*1000.0/N) << " ns/op (" << N << " ops)\n";
}

void BenchConcurrentReads() {
    DiskManager dm("bench_cr");
    BufferPoolManager bpm(&dm);
    BPlusTree tree(&bpm);
    int N = 5000;
    for (int i = 0; i < N; i++) tree.Insert(i, i * 10);

    int threads = 4;
    int ops_per_thread = 1000;
    std::atomic<long long> total_ns{0};
    std::vector<std::thread> workers;

    auto t0 = high_resolution_clock::now();
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&, t]() {
            rid_t v;
            auto s = high_resolution_clock::now();
            for (int i = 0; i < ops_per_thread; i++)
                tree.GetValue((i * 17 + t) % N, &v);
            auto e = high_resolution_clock::now();
            total_ns += duration_cast<nanoseconds>(e - s).count();
        });
    }
    for (auto &w : workers) w.join();
    auto t1 = high_resolution_clock::now();

    double avg = total_ns.load() / (double)(threads * ops_per_thread);
    double wall = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    std::cout << "4-thread read: " << avg << " ns/op ("
              << N << " keys, " << (threads*ops_per_thread) << " ops, "
              << wall << " ms wall)\n";
}

void BenchConcurrentMixed() {
    DiskManager dm("bench_cm");
    BufferPoolManager bpm(&dm);
    BPlusTree tree(&bpm);
    int N = 2000;
    for (int i = 0; i < N; i++) tree.Insert(i, i * 10);

    std::atomic<int> readers_done{0};
    std::atomic<int> writers_done{0};
    int ops_per_thread = 500;

    // 2 reader threads
    std::thread r1([&]() {
        rid_t v;
        for (int i = 0; i < ops_per_thread; i++) tree.GetValue(i % N, &v);
        readers_done++;
    });
    std::thread r2([&]() {
        rid_t v;
        for (int i = 0; i < ops_per_thread; i++) tree.GetValue((i * 31) % N, &v);
        readers_done++;
    });

    // 2 writer threads
    std::thread w1([&]() {
        for (int i = N; i < N + ops_per_thread; i++) tree.Insert(i, i * 10);
        writers_done++;
    });
    std::thread w2([&]() {
        for (int i = N + ops_per_thread; i < N + 2 * ops_per_thread; i++) tree.Insert(i, i * 10);
        writers_done++;
    });

    r1.join(); r2.join(); w1.join(); w2.join();
    std::cout << "2R+2W mixed: " << readers_done.load() << " readers, "
              << writers_done.load() << " writers finished\n";
}

void BenchRangeScan() {
    DiskManager dm("bench_rs");
    BufferPoolManager bpm(&dm);
    BPlusTree tree(&bpm);
    int N = 20000;
    for (int i = 0; i < N; i++) tree.Insert(i, i);
    
    int K = 30;
    auto t0 = high_resolution_clock::now();
    for (int k = 0; k < K; k++) tree.RangeScan(9000, 10000);
    auto t1 = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(t1 - t0).count();
    std::cout << "Range scan ~1000 rows: " << (us/(double)K) << " us/op (" << K << "x avg)\n";
}

int main() {
    TestInsertAndScan();
    TestRangeScan();
    TestRemove();
    std::cout << "\nAll B+Tree tests passed!\n\n";

    std::cout << "=== B+Tree Benchmark (degree=64, BufferPool 128 pages) ===\n";
    BenchInsert();
    BenchPointQuery();
    BenchConcurrentReads();
    BenchRangeScan();
    return 0;
}
