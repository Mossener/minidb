#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <sstream>

namespace minidb {

// ── AST ────────────────────────────────────────────

enum class SQLType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    DELETE,
    BEGIN_TXN,
    COMMIT_TXN,
    ABORT_TXN,
};

enum class ColType {
    INTEGER, FLOAT, VARCHAR, BOOLEAN
};

struct ColumnDef {
    std::string name;
    ColType type;
    size_t length = 0;
};

struct LiteralValue {
    enum ValType { INT, FLOAT, STRING };
    ValType type;
    std::string val;
    int64_t AsInt() const;
    double AsFloat() const;
    std::string AsString() const;
};

struct CompareCondition {
    enum Op { EQ, NE, LT, GT, LE, GE };
    std::string column;
    Op op;
    LiteralValue value;
};

struct SQLStatement {
    SQLType type;
    std::string table_name;

    // CREATE TABLE
    std::vector<ColumnDef> columns;

    // INSERT
    std::vector<std::vector<LiteralValue>> insert_values;

    // SELECT / DELETE
    std::unique_ptr<CompareCondition> where;
};

// ── Parser ─────────────────────────────────────────

SQLStatement ParseSQL(const std::string &sql);

// ── Helpers ────────────────────────────────────────

ColType ColumnTypeFromString(const std::string &s);

} // namespace minidb
