#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include "common/database.h"
#include "storage/tuple.h"
#include "storage/schema.h"
#include "storage/tuple_meta.h"

using namespace minidb;

Transaction *g_txn = nullptr;

void PrintHelp() {
    std::cout << "MiniDB Commands:\n"
              << "  CREATE TABLE <name>         - Create table with default schema\n"
              << "  INSERT <table> <id> <name> <salary> - Insert a row\n"
              << "  SELECT <table>              - Scan all rows (within current txn)\n"
              << "  DELETE <table> <id>         - Delete row by id\n"
              << "  CREATE INDEX <table>        - Create B+Tree index on id column\n"
              << "  FIND <table> <id>           - Find row by id (requires index)\n"
              << "  BEGIN                       - Start a transaction\n"
              << "  COMMIT                      - Commit current transaction\n"
              << "  ABORT                       - Abort current transaction\n"
              << "  HELP                        - Show this message\n"
              << "  EXIT / QUIT                 - Exit MiniDB\n";
}

void PrintTuple(const Tuple &t, const Schema &schema) {
    if (t.IsNull()) return;
    const char *data = t.GetData();
    int32_t id;
    memcpy(&id, data, sizeof(int32_t));
    std::string name(data + 4, 64);
    double salary;
    memcpy(&salary, data + 4 + 64, sizeof(double));

    auto null_pos = name.find('\0');
    if (null_pos != std::string::npos) name = name.substr(0, null_pos);

    std::cout << "| " << id << " | " << name << " | " << salary << " |\n";
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
    Schema default_schema(Schema::MakeDefaultSchema());

    std::cout << "MiniDB v2.0 - MVCC Relational Database\n";
    std::cout << "Type HELP for commands.\n\n";

    std::string line;
    while (true) {
        std::cout << "minidb> ";
        if (!std::getline(std::cin, line)) break;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "HELP" || cmd == "help") {
            PrintHelp();
        } else if (cmd == "EXIT" || cmd == "QUIT" || cmd == "exit" || cmd == "quit") {
            if (g_txn && g_txn->GetState() == TransactionState::RUNNING) {
                db.AbortTxn(g_txn);
            }
            break;
        } else if (cmd == "BEGIN" || cmd == "begin") {
            if (g_txn) {
                std::cout << "Transaction already active.\n";
                continue;
            }
            g_txn = db.BeginTxn();
            std::cout << "Transaction " << g_txn->GetTxnId() << " started (read_ts="
                      << g_txn->GetReadTs() << ").\n";
        } else if (cmd == "COMMIT" || cmd == "commit") {
            if (!g_txn) { std::cout << "No active transaction.\n"; continue; }
            db.CommitTxn(g_txn);
            std::cout << "Transaction " << g_txn->GetTxnId() << " committed.\n";
            g_txn = nullptr;
        } else if (cmd == "ABORT" || cmd == "abort") {
            if (!g_txn) { std::cout << "No active transaction.\n"; continue; }
            db.AbortTxn(g_txn);
            std::cout << "Transaction " << g_txn->GetTxnId() << " aborted.\n";
            g_txn = nullptr;
        } else if (cmd == "CREATE" || cmd == "create") {
            std::string type, name;
            iss >> type >> name;
            if (type == "TABLE" || type == "table") {
                if (db.CreateTable(name, default_schema)) {
                    std::cout << "Table '" << name << "' created.\n";
                } else {
                    std::cout << "ERROR: Table '" << name << "' already exists.\n";
                }
            } else if (type == "INDEX" || type == "index") {
                if (db.CreateIndex(name)) {
                    std::cout << "Index created on '" << name << "'.\n";
                } else {
                    std::cout << "ERROR: Table '" << name << "' not found.\n";
                }
            } else {
                std::cout << "ERROR: Unknown CREATE type.\n";
            }
        } else if (cmd == "INSERT" || cmd == "insert") {
            std::string table_name, name_val;
            int id_val;
            double salary_val;
            iss >> table_name >> id_val >> name_val >> salary_val;

            auto *tbl = db.GetTable(table_name);
            if (!tbl) { std::cout << "ERROR: Table not found.\n"; continue; }
            Schema *schema = tbl->schema;

            std::vector<char> data(schema->GetSerializedSize(), 0);
            memcpy(data.data(), &id_val, sizeof(int32_t));
            char name_buf[64] = {};
            memcpy(name_buf, name_val.c_str(), std::min(name_val.size(), size_t(63)));
            memcpy(data.data() + 4, name_buf, 64);
            memcpy(data.data() + 4 + 64, &salary_val, sizeof(double));

            Tuple tuple(std::move(data), *schema);
            Transaction *txn = GetTxn(db);
            if (db.Insert(table_name, tuple, txn)) {
                std::cout << "Inserted into '" << table_name << "'.\n";
            } else {
                std::cout << "ERROR: Insert failed.\n";
            }
        } else if (cmd == "SELECT" || cmd == "select") {
            std::string table_name;
            iss >> table_name;

            auto *tbl = db.GetTable(table_name);
            if (!tbl) { std::cout << "ERROR: Table not found.\n"; continue; }

            Transaction *txn = GetTxn(db);
            auto results = db.Scan(table_name, txn);
            std::cout << "(" << results.size() << " rows)\n";
            for (const auto &t : results) {
                PrintTuple(t, *tbl->schema);
            }
        } else if (cmd == "DELETE" || cmd == "delete") {
            std::string table_name;
            int64_t key;
            iss >> table_name >> key;
            Transaction *txn = GetTxn(db);
            if (db.Delete(table_name, key, txn)) {
                std::cout << "Deleted from '" << table_name << "'.\n";
            } else {
                std::cout << "Not found or table missing.\n";
            }
        } else if (cmd == "FIND" || cmd == "find") {
            std::string table_name;
            int64_t key;
            iss >> table_name >> key;
            auto *tbl = db.GetTable(table_name);
            if (!tbl || !tbl->has_index) {
                std::cout << "ERROR: Table or index not found.\n";
                continue;
            }
            rid_t rid;
            if (tbl->index->GetValue(key, &rid)) {
                Transaction *txn = GetTxn(db);
                Tuple t = tbl->heap->GetTuple(*tbl->schema, rid, txn, db.GetTxnMgr());
                if (!t.IsNull()) {
                    std::cout << "Found: ";
                    PrintTuple(t, *tbl->schema);
                } else {
                    std::cout << "Key not found (invisible).\n";
                }
            } else {
                std::cout << "Key not found.\n";
            }
        } else if (!line.empty()) {
            std::cout << "Unknown command. Type HELP.\n";
        }
    }

    if (g_txn && g_txn->GetState() == TransactionState::RUNNING) {
        db.AbortTxn(g_txn);
    }
    std::cout << "Bye!\n";
    return 0;
}
