# MiniDB

A lightweight relational database engine with MVCC (Multi-Version Concurrency Control) for educational purposes.

## Features

- **MVCC Transaction Engine**: Snapshot Isolation with timestamp-based visibility
- **Table Storage**: Page-based heap organization with tuple-level metadata
- **Buffer Pool**: LRU-kicked buffer management with page pinning
- **B+Tree Index**: Optional index for key-based lookups
- **Undo Log**: Write-set based rollback mechanism
- **REPL Shell**: Interactive CLI for running SQL-like commands

## Quick Start

```bash
mkdir build && cd build
cmake .. && make
./minidb
```

### REPL Commands

```
CREATE TABLE <name>         Create table with default schema
INSERT <table> <id> <name> <salary>  Insert a row
SELECT <table>              Scan all rows (within current txn)
DELETE <table> <id>         Delete row by id
CREATE INDEX <table>        Create B+Tree index on id column
FIND <table> <id>           Find row by id (requires index)
BEGIN                       Start a transaction
COMMIT                      Commit current transaction
ABORT                       Abort current transaction
HELP                        Show this message
EXIT / QUIT                 Exit MiniDB
```

### Example Session

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

## MVCC Architecture

Each tuple on disk carries a `TupleMeta` header:

```
[raw_data_size][txn_id][begin_ts][end_ts][raw_data]
```

- **begin_ts = 0**: uncommitted insert
- **end_ts = INT64_MAX**: tuple is still live
- **begin_ts > read_ts**: version created after reader's snapshot → invisible
- **end_ts <= read_ts**: version deleted before reader's snapshot → invisible

Transactions accumulate a `WriteRecord` vector (undo log) during execution, used both for commit finalization and abort rollback.

## Building & Testing

```bash
mkdir build && cd build
cmake .. && make
ctest --output-on-failure
```

Or run tests individually:

```bash
./test/table_heap_test    # MVCC isolation tests
./test/buffer_pool_test   # Buffer pool tests
./test/b_plus_tree_test   # B+Tree tests
```

## Project Structure

```
src/
├── common/          # Database orchestration (Begin/Commit/Abort)
├── execution/       # Executors (SeqScan, Insert, Delete)
├── index/           # B+Tree index
├── parser/          # SQL parser
├── storage/         # Disk manager, buffer pool, table heap, tuple
├── transaction/     # Transaction, TransactionManager, visibility
└── main.cpp         # REPL shell
test/                # Unit tests
```

## License

MIT
