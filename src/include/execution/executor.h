// 执行器模块 - 火山模型(Volcano Model)风格的查询算子
// 每个执行器实现 Init/Next 接口，通过组合实现查询计划的流水线执行

#pragma once

#include <memory>
#include <vector>
#include "common/config.h"
#include "storage/tuple.h"
#include "storage/schema.h"
#include "storage/table_heap.h"
#include "storage/buffer_pool_manager.h"
#include "index/b_plus_tree.h"
#include "transaction/transaction.h"

namespace minidb {

// 执行器上下文: 封装执行查询所需的所有组件引用
struct ExecutorContext {
    Transaction *transaction;       // 当前事务
    TransactionManager *txn_mgr;    // 事务管理器 (用于可见性判断)
    BufferPoolManager *bpm;         // 缓冲池管理器
    TableHeap *table_heap;          // 表堆 (数据访问)
    Schema *schema;                 // 表模式
    BPlusTree *index;               // B+树索引 (可选)

    ExecutorContext()
        : transaction(nullptr), txn_mgr(nullptr), bpm(nullptr),
          table_heap(nullptr), schema(nullptr), index(nullptr) {}
};

// 执行器基类: 所有算子的抽象接口 (火山模型)
class Executor {
public:
    explicit Executor(ExecutorContext *ctx) : ctx_(ctx) {}
    virtual ~Executor() = default;

    // Init: 初始化算子，准备迭代 (返回 true 表示成功)
    virtual bool Init() = 0;
    // Next: 获取下一个元组及其 RID，返回 false 表示迭代结束
    virtual bool Next(Tuple *tuple, rid_t *rid) = 0;

protected:
    ExecutorContext *ctx_;
};

// SeqScanExecutor: 顺序全表扫描算子
// 遍历表堆的页面链表，逐一返回对所有读事务可见的元组
class SeqScanExecutor : public Executor {
public:
    SeqScanExecutor(ExecutorContext *ctx, Schema *output_schema);
    bool Init() override;
    bool Next(Tuple *tuple, rid_t *rid) override;
private:
    Schema *output_schema_;   // 输出模式 (定义返回列结构)
    rid_t current_rid_;       // 当前扫描位置的 RID
    bool inited_;             // 是否已初始化
};

// InsertExecutor: 插入操作算子
// 将一组预构造的元组插入表堆，同时维护索引
class InsertExecutor : public Executor {
public:
    InsertExecutor(ExecutorContext *ctx, std::vector<Tuple> tuples);
    bool Init() override;
    bool Next(Tuple *tuple, rid_t *rid) override;
private:
    std::vector<Tuple> tuples_;  // 待插入的元组列表
    size_t idx_;                 // 当前插入进度索引
};

// DeleteExecutor: 删除操作算子 (包装子算子)
// 从子算子获取待删除元组，执行软删除并维护索引
class DeleteExecutor : public Executor {
public:
    DeleteExecutor(ExecutorContext *ctx, Executor *child);
    bool Init() override;
    bool Next(Tuple *tuple, rid_t *rid) override;
    ~DeleteExecutor() override;
private:
    Executor *child_;  // 子算子 (提供待删除的元组流)
};

// IndexScanExecutor: 索引扫描算子
// 利用 B+树索引通过 key 快速定位单个元组
class IndexScanExecutor : public Executor {
public:
    IndexScanExecutor(ExecutorContext *ctx, int64_t key);
    bool Init() override;
    bool Next(Tuple *tuple, rid_t *rid) override;
private:
    int64_t key_;      // 要查找的 key
    rid_t value_;      // 索引返回的 RID 值
    bool fetched_;     // 是否已经获取过 (索引扫描最多返回一条)
};

} // namespace minidb
