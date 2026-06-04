// MiniDB 主入口 - 基于 REPL 的命令行交互式 SQL 客户端
// 支持用户输入 SQL 语句并实时执行，查看结果

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include "linenoise.h"           // 命令行编辑和历史记录库
#include "common/database.h"
#include "storage/tuple.h"
#include "storage/schema.h"
#include "storage/tuple_meta.h"
#include "parser/parser.h"

using namespace minidb;

// 全局事务指针: 模拟交互式会话中活跃的事务
// auto-begin 模式: 首次 DML 操作时自动开启事务
Transaction *g_txn = nullptr;

// 打印帮助信息: 列出所有支持的命令和示例
void PrintHelp() {
    std::cout << "MiniDB SQL Commands:\n"
              << "  CREATE TABLE name (col TYPE, ...)    Create table\n"
              << "  INSERT INTO name VALUES (v1, v2, ...) Insert row(s)\n"
              << "  SELECT * FROM name [WHERE cond]       Query rows\n"
              << "  DELETE FROM name WHERE cond            Delete rows\n"
              << "  BEGIN                                 Start transaction\n"
              << "  COMMIT                                Commit transaction\n"
              << "  ABORT                                 Abort transaction\n"
              << "  HELP                                  Show this message\n"
              << "  EXIT / QUIT                           Exit MiniDB\n"
              << "\nTypes: INT, FLOAT, VARCHAR(n), BOOLEAN\n"
              << "Example: CREATE TABLE t (id INT, name VARCHAR(32), salary FLOAT)\n";
}

// PrintTuple: 将查询结果元组按列格式化输出到控制台
// 格式: | col1=val1 | col2='val2' | ...
void PrintTuple(const Tuple &t, const Schema &schema) {
    if (t.IsNull()) return;
    const char *data = t.GetData();

    std::cout << "|";
    for (size_t i = 0; i < schema.GetColumnCount(); i++) {
        const auto &col = schema.GetColumn(i);
        size_t offset = schema.GetColumnOffset(i);

        std::cout << " ";
        switch (col.type) {
            case TypeId::INTEGER: {
                int32_t v;
                memcpy(&v, data + offset, sizeof(int32_t));
                std::cout << col.name << "=" << v;
                break;
            }
            case TypeId::FLOAT: {
                double v;
                memcpy(&v, data + offset, sizeof(double));
                std::cout << col.name << "=" << v;
                break;
            }
            case TypeId::VARCHAR: {
                std::string s(data + offset, col.length);
                size_t null_pos = s.find('\0');
                if (null_pos != std::string::npos) s = s.substr(0, null_pos);
                std::cout << col.name << "='" << s << "'";
                break;
            }
            case TypeId::BOOLEAN: {
                bool v = (data[offset] != 0);
                std::cout << col.name << "=" << (v ? "true" : "false");
                break;
            }
        }
        std::cout << " |";
    }
    std::cout << "\n";
}

// GetTxn: 获取当前活跃事务，若不存在则自动开启一个新事务
// 实现 auto-begin 语义: INSERT/SELECT/DELETE 操作自动开启事务
Transaction *GetTxn(MiniDB &db) {
    if (!g_txn) {
        g_txn = db.BeginTxn();
        std::cout << "(auto-begin txn " << g_txn->GetTxnId() << ")\n";
    }
    return g_txn;
}

// 主函数: 初始化 MiniDB 实例，进入 REPL 循环
int main() {
    MiniDB db;

    linenoiseHistorySetMaxLen(100);  // 设置历史记录最大条数

    std::cout << "MiniDB v2.0 - MVCC Relational Database (SQL mode)\n";
    std::cout << "Type HELP for commands.\n\n";

    char *line;
    while ((line = linenoise("minidb> ")) != nullptr) {
        if (line[0] == '\0') { linenoiseFree(line); continue; }
        linenoiseHistoryAdd(line);  // 记录到命令历史

        std::string sql(line);
        linenoiseFree(line);

        // 去除首尾空白
        size_t start = sql.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = sql.find_last_not_of(" \t\r\n;");
        if (end != std::string::npos) sql = sql.substr(start, end - start + 1);
        else sql.clear();
        if (sql.empty()) continue;

        // 跳过纯注释行
        if (sql.size() >= 2 && sql[0] == '-' && sql[1] == '-') continue;

        std::string upper;
        for (char c : sql) upper.push_back(static_cast<char>(std::toupper(c)));

        // 处理非 SQL 的命令 (HELP / EXIT / QUIT)
        if (upper == "HELP") { PrintHelp(); continue; }
        if (upper == "EXIT" || upper == "QUIT") {
            if (g_txn && g_txn->GetState() == TransactionState::RUNNING)
                db.AbortTxn(g_txn);  // 退出前自动中止未提交的事务
            break;
        }

        // 解析并执行 SQL 语句
        try {
            SQLStatement stmt = ParseSQL(sql);

            switch (stmt.type) {
                case SQLType::CREATE_TABLE: {
                    if (db.ExecCreateTable(stmt))
                        std::cout << "Table '" << stmt.table_name << "' created.\n";
                    else
                        std::cout << "ERROR: Table '" << stmt.table_name << "' already exists.\n";
                    break;
                }
                case SQLType::CREATE_INDEX: {
                    if (db.CreateIndex(stmt.table_name))
                        std::cout << "Index created on '" << stmt.table_name << "'.\n";
                    else
                        std::cout << "ERROR: Table '" << stmt.table_name << "' not found.\n";
                    break;
                }
                case SQLType::INSERT: {
                    Transaction *txn = GetTxn(db);
                    if (db.ExecInsert(stmt, txn))
                        std::cout << "Inserted " << stmt.insert_values.size() << " row(s) into '" << stmt.table_name << "'.\n";
                    else
                        std::cout << "ERROR: Insert failed.\n";
                    break;
                }
                case SQLType::SELECT:
                case SQLType::EXPLAIN_SELECT: {
                    if (stmt.type == SQLType::EXPLAIN_SELECT) {
                        std::cout << db.ExplainSelect(stmt);
                    }
                    Transaction *txn = GetTxn(db);
                    auto results = db.ExecSelect(stmt, txn);
                    auto *tbl = db.GetTable(stmt.table_name);
                    std::cout << "(" << results.size() << " rows)\n";
                    if (tbl) {
                        for (const auto &tr : results)
                            PrintTuple(tr, *tbl->schema);
                    }
                    break;
                }
                case SQLType::DELETE: {
                    Transaction *txn = GetTxn(db);
                    int n = db.ExecDelete(stmt, txn);
                    std::cout << "Deleted " << n << " row(s) from '" << stmt.table_name << "'.\n";
                    break;
                }
                case SQLType::UPDATE: {
                    Transaction *txn = GetTxn(db);
                    int n = db.ExecUpdate(stmt, txn);
                    std::cout << "Updated " << n << " row(s) in '" << stmt.table_name << "'.\n";
                    break;
                }
                case SQLType::BEGIN_TXN: {
                    if (g_txn) {
                        std::cout << "Transaction already active.\n";
                    } else {
                        g_txn = db.BeginTxn();
                        std::cout << "Transaction " << g_txn->GetTxnId()
                                  << " started (read_ts=" << g_txn->GetReadTs() << ").\n";
                    }
                    break;
                }
                case SQLType::COMMIT_TXN: {
                    if (!g_txn) { std::cout << "No active transaction.\n"; break; }
                    db.CommitTxn(g_txn);
                    std::cout << "Transaction " << g_txn->GetTxnId() << " committed.\n";
                    g_txn = nullptr;  // 提交后清除事务引用
                    break;
                }
                case SQLType::ABORT_TXN: {
                    if (!g_txn) { std::cout << "No active transaction.\n"; break; }
                    db.AbortTxn(g_txn);
                    std::cout << "Transaction " << g_txn->GetTxnId() << " aborted.\n";
                    g_txn = nullptr;  // 回滚后清除事务引用
                    break;
                }
            }
        } catch (const std::exception &e) {
            std::cout << "ERROR: " << e.what() << "\n";
        }
    }

    // 程序退出时，自动中止未提交的事务
    if (g_txn && g_txn->GetState() == TransactionState::RUNNING)
        db.AbortTxn(g_txn);
    std::cout << "Bye!\n";
    return 0;
}
