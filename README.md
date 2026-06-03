# MiniDB

MiniDB 是一个从零手写的轻量级关系数据库，核心代码约 2500 行，完整实现了 **SQL 引擎**、**MVCC 事务引擎**、**缓冲池管理**、**B+Tree 索引** 以及 **交互式 REPL**。

项目定位为自学课程 cmu15-445构建数据库记录用途，零外部依赖（仅 linenoise 用于 REPL 行编辑）。

## 特性

- **SQL 引擎**：手写递归下降解析器，支持 CREATE TABLE / INSERT / SELECT / DELETE
- **MVCC 事务引擎**：基于时间戳的快照隔离（Snapshot Isolation），读写不互斥
- **Undo Log**：基于 WriteSet 的提交回写与回滚撤销机制
- **WHERE 过滤**：支持 `=`、`!=`、`<>`、`<`、`>`、`<=`、`>=` 条件过滤
- **表存储**：按页组织的堆表，每行附带事务元数据（TupleMeta）
- **缓冲池**：LRU 淘汰策略 + 页面钉住（pin）机制，支持脏页回写
- **B+Tree 索引**：可选的键值索引，支持等值查找
- **REPL 交互**：linenoise 行编辑，上下键历史回查，`--` 注释支持

## 环境要求

- C++17 兼容的编译器（GCC 7+、Clang 10+、MSVC 2019+）
- CMake 3.16+
- 支持线程库的操作系统（Linux、macOS、Windows）
- 推荐构建方式：**out-of-source**（项目外构建）

## 快速开始

```bash
cd /path/to/minidb
mkdir build && cd build
cmake .. && make -j$(nproc)
./minidb
```

## SQL 语法

```sql
CREATE TABLE name (col TYPE, ...)       -- 创建表，列类型：INT / FLOAT / VARCHAR(n) / BOOLEAN
INSERT INTO name VALUES (val, ...)      -- 单行或多行插入
SELECT * FROM name [WHERE cond]         -- 查询全表（支持 WHERE 过滤）
DELETE FROM name WHERE cond             -- 按条件删除行
BEGIN / COMMIT / ABORT                  -- 事务控制
HELP                                    -- 显示帮助
EXIT / QUIT                             -- 退出
```

支持操作符：`=`, `!=`, `<>`, `<`, `>`, `<=`, `>=`

> 无活跃事务时，INSERT / SELECT / DELETE 自动开启事务。

## 示例会话

```sql
minidb> CREATE TABLE employees (id INT, name VARCHAR(32), salary FLOAT);
Table 'employees' created.

minidb> INSERT INTO employees VALUES (1, 'Alice', 85000), (2, 'Bob', 72000);
(auto-begin txn 1)
Inserted 2 row(s) into 'employees'.

minidb> COMMIT;
Transaction 1 committed.

minidb> BEGIN;
Transaction 2 started (read_ts=3).

minidb> SELECT * FROM employees;
(2 rows)
| id=1 | name='Alice' | salary=85000 |
| id=2 | name='Bob' | salary=72000 |

minidb> INSERT INTO employees VALUES (3, 'Carol', 93000);
Inserted 1 row(s) into 'employees'.

minidb> ABORT;
Transaction 2 aborted.

-- Carol 被回滚，新事务只能看到已提交的数据
minidb> SELECT * FROM employees;
(auto-begin txn 3)
(2 rows)
| id=1 | name='Alice' | salary=85000 |
| id=2 | name='Bob' | salary=72000 |

-- WHERE 筛选高薪员工
minidb> SELECT * FROM employees WHERE salary > 80000;
(1 rows)
| id=1 | name='Alice' | salary=85000 |

minidb> DELETE FROM employees WHERE name = 'Bob';
(auto-begin txn 4)
Deleted 1 row(s) from 'employees'.

minidb> SELECT * FROM employees;
(1 rows)
| id=1 | name='Alice' | salary=85000 |

minidb> COMMIT;
Transaction 4 committed.
```

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

## MVCC 架构

每个 tuple 在磁盘上附带 `TupleMeta` 元数据头：

```
[raw_data_size : int32][txn_id : int64][begin_ts : int64][end_ts : int64][raw_data : variable]
```

可见性判断逻辑（`TransactionManager::IsVisible`）：

| 条件 | 结果 | 解释 |
|------|------|------|
| `begin_ts = 0` 且自己写的，`end_ts = MAX` | ✅ 可见 | 读自己的未提交插入 |
| `begin_ts = 0` 且自己写的，`end_ts != MAX` | ❌ 不可见 | 自己插入后又删了 |
| `begin_ts = 0` 且别人写的 | ❌ 不可见 | 脏数据 |
| `begin_ts > read_ts` | ❌ 不可见 | 在我启动之后才提交 |
| `end_ts != MAX 且 end_ts <= read_ts` | ❌ 不可见 | 在我启动时已被删除 |
| `IsActive(txn_id)` = true | ❌ 不可见 | 创建者还没提交 |
| 其他情况 | ✅ 可见 | 正常已提交的存活版本 |

事务执行中累积 `WriteRecord`（Undo Log），提交时回写 `begin_ts`/`end_ts`，回滚时撤销操作。

## 目录结构

```
src/
├── main.cpp                     # REPL 交互界面
├── linenoise.c                  # 行编辑库（上下键历史）
│
├── common/
│   └── database.cpp             # MiniDB 主类：建表、增删查、事务编排
├── execution/
│   └── executor.cpp             # 执行器：SeqScan / Insert / Delete / IndexScan
├── index/
│   └── b_plus_tree.cpp          # B+Tree 索引
├── parser/
│   └── parser.cpp               # SQL 词法分析 + 递归下降解析
├── storage/
│   ├── buffer_pool_manager.cpp  # 缓冲池（LRU 淘汰 + 脏页回写）
│   ├── disk_manager.cpp         # 磁盘页读写
│   ├── lru_replacer.cpp         # LRU 替换策略
│   ├── schema.cpp               # 表结构定义
│   ├── table_heap.cpp           # 堆表：Insert / Delete / Get / Scan
│   └── tuple.cpp                # Tuple 序列化
└── transaction/
    └── transaction.cpp          # 事务生命周期 + IsVisible 可见性

test/
├── table_heap_test.cpp          # MVCC 隔离性测试
├── buffer_pool_test.cpp         # 缓冲池测试
└── b_plus_tree_test.cpp         # B+Tree 测试
```

## License

MIT
