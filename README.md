# MiniDB

一个用于教学目的的轻量级关系数据库，实现了基于 MVCC 的事务引擎。

## 特性

- **MVCC 事务引擎**：基于时间戳的快照隔离（Snapshot Isolation）
- **表存储**：按页组织的堆表，每行附带事务元数据
- **缓冲池**：LRU 淘汰策略 + 页面钉住机制
- **B+Tree 索引**：可选的键值索引，支持等值查找
- **Undo Log**：基于 WriteSet 的回滚机制
- **REPL 交互**：命令行界面，支持类 SQL 操作

## 环境要求

- C++17 兼容的编译器（GCC 7+、Clang 10+、MSVC 2019+）
- CMake 3.16+
- 支持线程库的操作系统（Linux、macOS、Windows）
- 推荐构建方式：**out-of-source**（项目外构建）

## 快速开始

```bash
mkdir build && cd build
cmake .. && make
./minidb
```

### 交互命令

```
CREATE TABLE <name>          创建默认 schema 的表
INSERT <table> <id> <name> <salary>  插入一行
SELECT <table>               扫描表中的所有行（当前事务内）
DELETE <table> <id>          按 id 删除行
CREATE INDEX <table>         在 id 列上创建 B+Tree 索引
FIND <table> <id>            通过索引按 id 查找
BEGIN                        开始一个事务
COMMIT                       提交当前事务
ABORT                        回滚当前事务
HELP                         显示帮助信息
EXIT / QUIT                  退出 MiniDB
```

### 示例会话

```
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
minidb> COMMIT
Transaction 2 committed.
```

## MVCC 架构

每行数据在磁盘上附带 `TupleMeta` 元数据头：

```
[raw_data_size][txn_id][begin_ts][end_ts][raw_data]
```

可见性规则：

- **begin_ts = 0**：未提交的插入
- **end_ts = INT64_MAX**：行仍然有效（未被删除）
- **begin_ts > read_ts**：版本在读取者的快照之后创建 → 不可见
- **end_ts <= read_ts**：版本在读取者的快照之前已被删除 → 不可见

事务在执行过程中累加 `WriteRecord` 向量（Undo Log），提交时用于回写元数据，回滚时用于撤销操作。

## 编译与测试

```bash
mkdir build && cd build
cmake .. && make
ctest --output-on-failure
```

单独运行测试：

```bash
./test/table_heap_test    # MVCC 隔离性测试
./test/buffer_pool_test   # 缓冲池测试
./test/b_plus_tree_test   # B+Tree 测试
```

### 清理构建

```bash
rm -rf build/
```

## 目录结构

```
src/
├── common/          # 数据库编排（Begin/Commit/Abort）
├── execution/       # 执行器（SeqScan, Insert, Delete）
├── index/           # B+Tree 索引
├── parser/          # SQL 解析器
├── storage/         # 磁盘管理、缓冲池、堆表、元组
├── transaction/     # 事务、事务管理器、可见性判断
└── main.cpp         # REPL 交互界面
test/                # 单元测试
```

## License

MIT
