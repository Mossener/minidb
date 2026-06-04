// SQL 解析器模块 - AST 节点定义和解析入口
// 负责将 SQL 文本字符串解析为 SQLStatement 抽象语法树

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <sstream>

namespace minidb {

// ── AST 抽象语法树节点定义 ─────────────────────────

// SQL 语句类型枚举
enum class SQLType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    DELETE,
    UPDATE,
    BEGIN_TXN,
    COMMIT_TXN,
    ABORT_TXN,
};

// 列数据类型枚举 (解析器层面)
enum class ColType {
    INTEGER, FLOAT, VARCHAR, BOOLEAN
};

// 列定义: CREATE TABLE 中的列描述 (列名、类型、长度)
struct ColumnDef {
    std::string name;   // 列名
    ColType type;       // 列类型
    size_t length = 0;  // 长度 (仅 VARCHAR 有效)
};

// 字面值: 表示 SQL 语句中的常量值 (整数、浮点数或字符串)
struct LiteralValue {
    enum ValType { INT, FLOAT, STRING };
    ValType type;       // 值的实际类型
    std::string val;    // 字符串形式的原始值
    int64_t AsInt() const;
    double AsFloat() const;
    std::string AsString() const;
};

// 比较条件: WHERE 子句中的 "column op value" 条件
struct CompareCondition {
    enum Op { EQ, NE, LT, GT, LE, GE };
    std::string column;  // 参与比较的列名
    Op op;               // 比较运算符
    LiteralValue value;  // 比较的右侧值
};

// UPDATE: WHERE 条件 + SET 赋值列表
struct UpdateAssignment {
    std::string column;   // 要更新的列名
    LiteralValue value;   // 新值
};

// SQL 语句 AST: 一条完整 SQL 语句的解析结果
struct SQLStatement {
    SQLType type;
    std::string table_name;

    std::vector<ColumnDef> columns;
    std::vector<std::vector<LiteralValue>> insert_values;
    std::unique_ptr<CompareCondition> where;

    // UPDATE: SET 赋值列表
    std::vector<UpdateAssignment> assignments;
};

// ── 解析器入口 ─────────────────────────────────────

// ParseSQL: 将 SQL 字符串解析为 SQLStatement AST
// 抛出 std::runtime_error 当语法错误时
SQLStatement ParseSQL(const std::string &sql);

// ── 辅助函数 ───────────────────────────────────────

// 将类型名称字符串 (如 "INT", "VARCHAR") 转换为 ColType 枚举
ColType ColumnTypeFromString(const std::string &s);

} // namespace minidb
