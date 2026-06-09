#include <iostream>
#include <cassert>
#include <chrono>
#include <iomanip>
#include "index/b_plus_tree.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
using namespace minidb;
using namespace std::chrono;

int main() {
    // B+Tree 插入
    {
        DiskManager dm("b1");
        BufferPoolManager bpm(&dm);
        BPlusTree tree(&bpm);
        int N = 30000;
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) tree.Insert(i, i * 10);
        auto t1 = high_resolution_clock::now();
        auto us = duration_cast<microseconds>(t1 - t0).count();
        std::cout << "B+Tree Insert:   " << std::setw(6) << (us*1000.0/N) << " ns/op (" << N << " ops)\n";
    }

    // B+Tree 点查
    {
        DiskManager dm("b2");
        BufferPoolManager bpm(&dm);
        BPlusTree tree(&bpm);
        for (int i = 0; i < 20000; i++) tree.Insert(i, i * 10);
        rid_t v; int N = 15000;
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) tree.GetValue(i, &v);
        auto t1 = high_resolution_clock::now();
        auto ns = duration_cast<nanoseconds>(t1 - t0).count();
        std::cout << "B+Tree PointQ:   " << std::setw(6) << (ns/(double)N) << " ns/op (" << N << " ops)\n";
    }

    // B+Tree 范围扫描
    {
        DiskManager dm("b3");
        BufferPoolManager bpm(&dm);
        BPlusTree tree(&bpm);
        for (int i = 0; i < 20000; i++) tree.Insert(i, i);
        int N = 40;
        auto t0 = high_resolution_clock::now();
        for (int k = 0; k < N; k++) tree.RangeScan(5000, 6000);
        auto t1 = high_resolution_clock::now();
        auto us = duration_cast<microseconds>(t1 - t0).count();
        std::cout << "B+Tree Range1K:  " << std::setw(6) << (us*1000.0/N) << " us/op (" << N << " ops)\n";
    }

    // B+Tree 删除
    {
        DiskManager dm("b4");
        BufferPoolManager bpm(&dm);
        BPlusTree tree(&bpm);
        for (int i = 0; i < 20000; i++) tree.Insert(i, i);
        int N = 5000;
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < N; i++) tree.Remove(i);
        auto t1 = high_resolution_clock::now();
        auto us = duration_cast<microseconds>(t1 - t0).count();
        std::cout << "B+Tree Remove:   " << std::setw(6) << (us*1000.0/N) << " ns/op (" << N << " ops)\n";
    }

    return 0;
}
