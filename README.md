# MiniDB

MiniDB 是一个从零手写的轻量级关系数据库，核心代码约 2000 行，完整实现了 **MVCC 事务引擎**、**缓冲池管理**、**B+Tree 索引** 以及 **交互式 REPL**。

项目定位为教学用途，代码结构清晰、依赖少，适合用于学习数据库内核原理。

## 特性

- **MVCC 事务引擎**：基于时间戳的快照隔离（Snapshot Isolation），读写不互斥
- **Undo Log**：基于 WriteSet 的提交回写与回滚撤销机制
- **表存储**：按页组织的堆表，每行附带事务元数据（TupleMeta）
- **缓冲池**：LRU 淘汰策略 + 页面钉住（pin）机制，支持脏页回写
- **B+Tree 索引**：可选的键值索引，支持等值查找
- **REPL 交互**：命令行界面，支持类 SQL 操作，自动/手动事务管理

## 环境要求

- C++17 兼容的编译器（GCC 7+、Clang 10+、MSVC 2019+）
- CMake 3.16+
- 支持线程库的操作系统（Linux、macOS、Windows）
- 推荐构建方式：**out-of-source**（项目外构建）

## 快速开始

```bash
# 编译
cd /path/to/minidb
mkdir build && cd build
cmake .. && make -j$(nproc)

# 启动交互式 REPL
./minidb
```

进入 REPL 后输入 `HELP` 查看所有命令，或参考下方示例会话。

### 交互命令

```
CREATE TABLE <name>                    创建默认 schema 的表
INSERT <table> <id> <name> <salary>    插入一行（id:int, name:varchar(64), salary:double）
SELECT <table>                         扫描表中的所有行（在当前事务内）
DELETE <table> <id>                    按 id 删除行
CREATE INDEX <table>                   在 id 列上创建 B+Tree 索引
FIND <table> <id>                      通过索引按 id 查找（需先 CREATE INDEX）
BEGIN                                  开始一个手动事务
COMMIT                                 提交当前事务
ABORT                                  回滚当前事务
HELP                                   显示帮助信息
EXIT / QUIT                            退出 MiniDB
```

> 如果没有活跃事务，执行 INSERT / SELECT / DELETE / FIND 时会自动开启一个事务。

### 示例会话

```text
minidb> CREATE TABLE employees
Table 'employees' created.

minidb> INSERT employees 1 Alice 85000
(auto-begin txn 1)
Inserted into 'employees'.

minidb> INSERT employees 2 Bob 72000
Inserted into 'employees'.

minidb> COMMIT
Transaction 1 committed.

minidb> BEGIN
Transaction 2 started (read_ts=4).

minidb> SELECT employees
(2 rows)
| 1 | Alice | 85000 |
| 2 | Bob | 72000 |

minidb> INSERT employees 3 Carol 93000
Inserted into 'employees'.

minidb> ABORT
Transaction 2 aborted.

minidb> SELECT employees
(auto-begin txn 3)
(2 rows)
| 1 | Alice | 85000 |
| 2 | Bob | 72000 |
```

事务 2 插入的 Carol 在 ABORT 后被回滚，事务 3 只能看到已提交的 Alice 和 Bob —— MVCC 隔离性在起作用。

### 使用现有构建（无需重新编译）

```bash
cd /home/yurika/minidb/build
./minidb
cd /home/yurika/minidb/build
./test/table_heap_test     # MVCC 隔离性测试
./test/buffer_pool_test    # 缓冲池测试
./test/b_plus_tree_test    # B+Tree 测试
```

## MVCC 架构

每个 tuple 在磁盘上附带 `TupleMeta` 元数据头：

```
[raw_data_size : int32][txn_id : int64][begin_ts : int64][end_ts : int64][raw_data : variable]
```

可见性判断逻辑（`TransactionManager::IsVisible`）：

| 条件 | 结果 | 解释 |
|------|------|------|
| `begin_ts = 0` 且是自己写的 | ✅ 可见 | 读自己的未提交写入 |
| `begin_ts = 0` 且是别人写的 | ❌ 不可见 | 脏数据 |
| `begin_ts > read_ts` | ❌ 不可见 | 在我启动之后才提交 |
| `end_ts != MAX 且 end_ts <= read_ts` | ❌ 不可见 | 在我启动时已被删除 |
| `IsActive(txn_id)` = true | ❌ 不可见 | 创建者还没提交 |
| 其他情况 | ✅ 可见 | 正常已提交的存活版本 |

事务在执行过程中累积 `WriteRecord`（Undo Log），提交时用于回写 `begin_ts`/`end_ts`，回滚时用于撤销操作。

## 编译与测试

```bash
cd /path/to/minidb
mkdir build && cd build
cmake .. && make -j$(nproc)

# 运行所有测试
ctest --output-on-failure

# 单独运行测试
./test/table_heap_test     # MVCC 隔离性测试
./test/buffer_pool_test    # 缓冲池测试
./test/b_plus_tree_test    # B+Tree 测试
```

### 清理构建

```bash
rm -rf build/
```

## 目录结构

```
src/
├── common/          # 数据库编排（BeginTxn / CommitTxn / AbortTxn）
├── execution/       # 执行器（SeqScan, Insert, Delete）
├── index/           # B+Tree 索引
├── parser/          # SQL 解析器
├── storage/         # 磁盘管理、缓冲池、堆表、Tuple 序列化
├── transaction/     # 事务、事务管理器、可见性判断（IsVisible）
└── main.cpp         # REPL 交互界面
test/                # 单元测试
```

## License

MIT
