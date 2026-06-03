#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include "linenoise.h"
#include "common/database.h"
#include "storage/tuple.h"
#include "storage/schema.h"
#include "storage/tuple_meta.h"
#include "parser/parser.h"

using namespace minidb;

Transaction *g_txn = nullptr;

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

Transaction *GetTxn(MiniDB &db) {
    if (!g_txn) {
        g_txn = db.BeginTxn();
        std::cout << "(auto-begin txn " << g_txn->GetTxnId() << ")\n";
    }
    return g_txn;
}

int main() {
    MiniDB db;

    linenoiseHistorySetMaxLen(100);

    std::cout << "MiniDB v2.0 - MVCC Relational Database (SQL mode)\n";
    std::cout << "Type HELP for commands.\n\n";

    char *line;
    while ((line = linenoise("minidb> ")) != nullptr) {
        if (line[0] == '\0') { linenoiseFree(line); continue; }
        linenoiseHistoryAdd(line);

        std::string sql(line);
        linenoiseFree(line);

        // Trim
        size_t start = sql.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = sql.find_last_not_of(" \t\r\n;");
        if (end != std::string::npos) sql = sql.substr(start, end - start + 1);
        else sql.clear();
        if (sql.empty()) continue;

        // Skip pure comment lines
        if (sql.size() >= 2 && sql[0] == '-' && sql[1] == '-') continue;

        std::string upper;
        for (char c : sql) upper.push_back(static_cast<char>(std::toupper(c)));

        // Handle non-SQL commands
        if (upper == "HELP") { PrintHelp(); continue; }
        if (upper == "EXIT" || upper == "QUIT") {
            if (g_txn && g_txn->GetState() == TransactionState::RUNNING)
                db.AbortTxn(g_txn);
            break;
        }

        // Parse and execute SQL
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
                case SQLType::INSERT: {
                    Transaction *txn = GetTxn(db);
                    if (db.ExecInsert(stmt, txn))
                        std::cout << "Inserted " << stmt.insert_values.size() << " row(s) into '" << stmt.table_name << "'.\n";
                    else
                        std::cout << "ERROR: Insert failed.\n";
                    break;
                }
                case SQLType::SELECT: {
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
                    g_txn = nullptr;
                    break;
                }
                case SQLType::ABORT_TXN: {
                    if (!g_txn) { std::cout << "No active transaction.\n"; break; }
                    db.AbortTxn(g_txn);
                    std::cout << "Transaction " << g_txn->GetTxnId() << " aborted.\n";
                    g_txn = nullptr;
                    break;
                }
            }
        } catch (const std::exception &e) {
            std::cout << "ERROR: " << e.what() << "\n";
        }
    }

    if (g_txn && g_txn->GetState() == TransactionState::RUNNING)
        db.AbortTxn(g_txn);
    std::cout << "Bye!\n";
    return 0;
}
