// SQL 解析器实现 - 词法分析(Lexer)和语法分析(Parser)

#include "parser/parser.h"
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace minidb {

// ── LiteralValue 辅助方法 ───────────────────────

// 将字符串值转换为 int64_t
int64_t LiteralValue::AsInt() const {
    return std::stoll(val);
}

// 将字符串值转换为 double
double LiteralValue::AsFloat() const {
    return std::stod(val);
}

// 返回原始字符串值
std::string LiteralValue::AsString() const {
    return val;
}

// 将类型字符串 (如 "INT", "VARCHAR") 转换为 ColType 枚举
// 忽略大小写，支持常见的类型别名
ColType ColumnTypeFromString(const std::string &s) {
    std::string u;
    for (char c : s) u.push_back(static_cast<char>(std::toupper(c)));
    if (u == "INT" || u == "INTEGER") return ColType::INTEGER;
    if (u == "FLOAT" || u == "DOUBLE" || u == "REAL") return ColType::FLOAT;
    if (u == "VARCHAR" || u == "TEXT" || u == "STRING") return ColType::VARCHAR;
    if (u == "BOOLEAN" || u == "BOOL") return ColType::BOOLEAN;
    throw std::runtime_error("Unknown type: " + s);
}

// ── Token 定义 ─────────────────────────────────

// Token 类型枚举: 词法分析器输出的一级符号类型
enum class Tok {
    CREATE, TABLE, INSERT, INTO, VALUES, SELECT, FROM, DELETE, UPDATE, SET,
    WHERE, AND, OR, BEGIN_TOK, COMMIT_TOK, ABORT_TOK, DROP, INDEX,
    IDENT, NUMBER, STRING, BOOL_LIT, STAR, LPAREN, RPAREN, COMMA, SEMI,
    EQ, NE, LT, GT, LE, GE, END, UNKNOWN
};

// Token: 词法单元，包含类型和原始文本
struct Token {
    Tok type;
    std::string text;
};

// 将字符串转为小写，用于关键字不区分大小写的匹配
static std::string tok_lower(const std::string &s) {
    std::string r;
    for (char c : s) r.push_back(static_cast<char>(std::tolower(c)));
    return r;
}

// 判断标识符是否为关键字，返回对应的 Token 类型
// 如果不是关键字则返回 Tok::IDENT (标识符)
static Tok keyword_tok(const std::string &s) {
    std::string low = tok_lower(s);
    if (low == "create") return Tok::CREATE;
    if (low == "table") return Tok::TABLE;
    if (low == "insert") return Tok::INSERT;
    if (low == "into") return Tok::INTO;
    if (low == "values") return Tok::VALUES;
    if (low == "select") return Tok::SELECT;
    if (low == "from") return Tok::FROM;
    if (low == "delete") return Tok::DELETE;
    if (low == "update") return Tok::UPDATE;
    if (low == "set") return Tok::SET;
    if (low == "where") return Tok::WHERE;
    if (low == "and") return Tok::AND;
    if (low == "or") return Tok::OR;
    if (low == "begin") return Tok::BEGIN_TOK;
    if (low == "commit") return Tok::COMMIT_TOK;
    if (low == "abort") return Tok::ABORT_TOK;
    if (low == "drop") return Tok::DROP;
    if (low == "index") return Tok::INDEX;
    return Tok::IDENT;
}

// ── 词法分析器 (Lexer) ───────────────────────────

class Lexer {
public:
    explicit Lexer(const std::string &s) : input(s), pos(0) {}

    // 读取下一个 Token: 跳过空白、注释，根据首字符判断 Token 类型
    Token next() {
        skip_ws();
        if (pos >= input.size()) return {Tok::END, ""};

        char c = input[pos];

        // 行注释 -- : 跳过直到换行符
        if (c == '-' && pos + 1 < input.size() && input[pos + 1] == '-') {
            while (pos < input.size() && input[pos] != '\n') pos++;
            return next();
        }

        // 单字符 Token
        if (c == ';') { pos++; return {Tok::SEMI, ";"}; }
        if (c == '*') { pos++; return {Tok::STAR, "*"}; }
        if (c == '(') { pos++; return {Tok::LPAREN, "("}; }
        if (c == ')') { pos++; return {Tok::RPAREN, ")"}; }
        if (c == ',') { pos++; return {Tok::COMMA, ","}; }
        if (c == '=') { pos++; return {Tok::EQ, "="}; }

        // != 不等于
        if (c == '!' && pos + 1 < input.size() && input[pos + 1] == '=') {
            pos += 2; return {Tok::NE, "!="};
        }
        // <, <=, <> (另一个不等于写法)
        if (c == '<') {
            if (pos + 1 < input.size() && input[pos + 1] == '=') { pos += 2; return {Tok::LE, "<="}; }
            if (pos + 1 < input.size() && input[pos + 1] == '>') { pos += 2; return {Tok::NE, "<>"}; }
            pos++; return {Tok::LT, "<"};
        }
        // >, >=
        if (c == '>') {
            if (pos + 1 < input.size() && input[pos + 1] == '=') { pos += 2; return {Tok::GE, ">="}; }
            pos++; return {Tok::GT, ">"};
        }

        // 字符串字面值: 被单引号或双引号包围，支持转义字符
        if (c == '\'' || c == '"') {
            char quote = c;
            pos++;
            size_t start = pos;
            while (pos < input.size() && input[pos] != quote) {
                if (input[pos] == '\\') pos++; // 跳过转义字符
                pos++;
            }
            std::string text = input.substr(start, pos - start);
            if (pos < input.size()) pos++; // 跳过结束引号
            return {Tok::STRING, text};
        }

        // 数字: 可选的负号 + 数字和小数点
        if (std::isdigit(c) || (c == '-' && pos + 1 < input.size() && std::isdigit(input[pos + 1]))) {
            size_t start = pos;
            if (c == '-') pos++;
            while (pos < input.size() && (std::isdigit(input[pos]) || input[pos] == '.')) pos++;
            return {Tok::NUMBER, input.substr(start, pos - start)};
        }

        // 标识符或关键字: 以字母或下划线开头
        if (std::isalpha(c) || c == '_') {
            size_t start = pos;
            while (pos < input.size() && (std::isalnum(input[pos]) || input[pos] == '_')) pos++;
            std::string word = input.substr(start, pos - start);
            std::string low = tok_lower(word);
            if (low == "true" || low == "false") return {Tok::BOOL_LIT, word};
            Tok kw = keyword_tok(word);
            return {kw, word};
        }

        // 无法识别的字符
        pos++;
        return {Tok::UNKNOWN, std::string(1, c)};
    }

private:
    std::string input;  // 输入 SQL 字符串
    size_t pos;         // 当前读取位置

    // 跳过空白字符: 空格、制表符、换行、回车
    void skip_ws() {
        while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' ||
               input[pos] == '\n' || input[pos] == '\r')) pos++;
    }
};

// ── 语法分析器 (Parser) ───────────────────────────

// 递归下降解析器: 从 Lexer 获取 Token 流，构建 AST
class Parser {
public:
    explicit Parser(Lexer &l) : lex(l) { cur = lex.next(); }

    // 解析入口: 根据首个 Token 类型分发到对应的解析函数
    SQLStatement parse() {
        switch (cur.type) {
            case Tok::CREATE: return parse_create_table();
            case Tok::INSERT: return parse_insert();
            case Tok::SELECT: return parse_select();
            case Tok::DELETE: return parse_delete();
            case Tok::UPDATE: return parse_update();
            case Tok::BEGIN_TOK: return parse_begin();
            case Tok::COMMIT_TOK: return parse_commit();
            case Tok::ABORT_TOK: return parse_abort();
            default:
                throw std::runtime_error("Unexpected token: '" + cur.text + "'");
        }
    }

private:
    Lexer &lex;   // 词法分析器引用
    Token cur;    // 当前 Token (lookahead 1)

    void advance() { cur = lex.next(); }

    // 期望当前 Token 为指定类型，是则前进，否则抛出语法错误
    void expect(Tok t) {
        if (cur.type != t)
            throw std::runtime_error("Syntax error: expected token " +
                std::to_string(static_cast<int>(t)) + " but got '" + cur.text + "'");
        advance();
    }

    // 解析 CREATE TABLE name ( col TYPE, ... )
    SQLStatement parse_create_table() {
        advance(); // 跳过 CREATE
        expect(Tok::TABLE);
        SQLStatement stmt;
        stmt.type = SQLType::CREATE_TABLE;
        stmt.table_name = cur.text;
        expect(Tok::IDENT);
        expect(Tok::LPAREN);

        // 循环解析列定义，直到遇到 )
        while (cur.type != Tok::RPAREN) {
            ColumnDef cd;
            cd.name = cur.text;
            expect(Tok::IDENT);
            cd.type = ColumnTypeFromString(cur.text);
            advance(); // 消费类型关键字

            // VARCHAR 类型需要解析括号内的长度参数
            if (cd.type == ColType::VARCHAR && cur.type == Tok::LPAREN) {
                advance(); // 消费 '('
                cd.length = static_cast<size_t>(std::stoul(cur.text));
                expect(Tok::NUMBER);
                expect(Tok::RPAREN);
            } else if (cd.type == ColType::VARCHAR) {
                cd.length = 64; // VARCHAR 无显式长度时使用默认值
            }

            stmt.columns.push_back(cd);

            if (cur.type == Tok::COMMA) advance();
        }
        expect(Tok::RPAREN);
        if (cur.type == Tok::SEMI) advance();
        return stmt;
    }

    // 解析 INSERT INTO table VALUES (val, ...) [, (val, ...)]
    // 支持多行插入
    SQLStatement parse_insert() {
        advance(); // 跳过 INSERT
        expect(Tok::INTO);
        SQLStatement stmt;
        stmt.type = SQLType::INSERT;
        stmt.table_name = cur.text;
        expect(Tok::IDENT);
        expect(Tok::VALUES);

        // 循环解析多行值: (val1, val2, ...)
        do {
            expect(Tok::LPAREN);
            std::vector<LiteralValue> row;
            while (cur.type != Tok::RPAREN) {
                row.push_back(parse_literal());
                if (cur.type == Tok::COMMA) advance();
            }
            expect(Tok::RPAREN);
            stmt.insert_values.push_back(std::move(row));

            if (cur.type == Tok::COMMA) advance();
        } while (cur.type == Tok::LPAREN);

        if (cur.type == Tok::SEMI) advance();
        return stmt;
    }

    // 解析 SELECT * FROM table [WHERE cond]
    SQLStatement parse_select() {
        advance(); // 跳过 SELECT
        SQLStatement stmt;
        stmt.type = SQLType::SELECT;

        if (cur.type == Tok::STAR) {
            advance(); // 跳过 * (目前只支持 SELECT *)
        } else {
            // 列表达式暂时不支持，直接跳过至 FROM
            while (cur.type != Tok::FROM) advance();
        }

        expect(Tok::FROM);
        stmt.table_name = cur.text;
        expect(Tok::IDENT);

        // 可选的 WHERE 子句
        if (cur.type == Tok::WHERE) {
            advance();
            stmt.where = parse_condition();
        }

        if (cur.type == Tok::SEMI) advance();
        return stmt;
    }

    // 解析 DELETE FROM table WHERE cond (WHERE 是强制要求)
    SQLStatement parse_delete() {
        advance(); // 跳过 DELETE
        expect(Tok::FROM);
        SQLStatement stmt;
        stmt.type = SQLType::DELETE;
        stmt.table_name = cur.text;
        expect(Tok::IDENT);

        if (cur.type == Tok::WHERE) {
            advance();
            stmt.where = parse_condition();
        } else {
            throw std::runtime_error("DELETE requires a WHERE clause");
        }

        if (cur.type == Tok::SEMI) advance();
        return stmt;
    }

    // 解析 UPDATE table SET col=val,... WHERE cond
    SQLStatement parse_update() {
        advance(); // skip UPDATE
        SQLStatement stmt;
        stmt.type = SQLType::UPDATE;
        stmt.table_name = cur.text;
        expect(Tok::IDENT);
        expect(Tok::SET);

        // SET col = val, col2 = val2, ...
        do {
            UpdateAssignment asgn;
            asgn.column = cur.text;
            expect(Tok::IDENT);
            expect(Tok::EQ);
            asgn.value = parse_literal();
            stmt.assignments.push_back(std::move(asgn));
            if (cur.type == Tok::COMMA) advance();
        } while (cur.type == Tok::IDENT);

        if (cur.type == Tok::WHERE) {
            advance();
            stmt.where = parse_condition();
        }

        if (cur.type == Tok::SEMI) advance();
        return stmt;
    }

    // 解析 BEGIN 语句 (开始事务)
    SQLStatement parse_begin() {
        advance();
        if (cur.type == Tok::SEMI) advance();
        SQLStatement stmt;
        stmt.type = SQLType::BEGIN_TXN;
        return stmt;
    }

    // 解析 COMMIT 语句 (提交事务)
    SQLStatement parse_commit() {
        advance();
        if (cur.type == Tok::SEMI) advance();
        SQLStatement stmt;
        stmt.type = SQLType::COMMIT_TXN;
        return stmt;
    }

    // 解析 ABORT 语句 (回滚事务)
    SQLStatement parse_abort() {
        advance();
        if (cur.type == Tok::SEMI) advance();
        SQLStatement stmt;
        stmt.type = SQLType::ABORT_TXN;
        return stmt;
    }

    // 解析 WHERE 比较条件: column op literal
    std::unique_ptr<CompareCondition> parse_condition() {
        auto cond = std::make_unique<CompareCondition>();
        cond->column = cur.text;
        expect(Tok::IDENT);

        // 确定比较运算符
        switch (cur.type) {
            case Tok::EQ: cond->op = CompareCondition::EQ; break;
            case Tok::NE: cond->op = CompareCondition::NE; break;
            case Tok::LT: cond->op = CompareCondition::LT; break;
            case Tok::GT: cond->op = CompareCondition::GT; break;
            case Tok::LE: cond->op = CompareCondition::LE; break;
            case Tok::GE: cond->op = CompareCondition::GE; break;
            default: throw std::runtime_error("Expected comparison operator");
        }
        advance();
        cond->value = parse_literal();
        return cond;
    }

    // 解析字面值: 数字(整数或浮点)、字符串、布尔
    LiteralValue parse_literal() {
        LiteralValue lv;
        if (cur.type == Tok::NUMBER) {
            std::string t = cur.text;
            // 含小数点则为浮点数，否则为整数
            if (t.find('.') != std::string::npos) {
                lv.type = LiteralValue::FLOAT;
            } else {
                lv.type = LiteralValue::INT;
            }
            lv.val = t;
            advance();
        } else if (cur.type == Tok::STRING) {
            lv.type = LiteralValue::STRING;
            lv.val = cur.text;
            advance();
        } else if (cur.type == Tok::BOOL_LIT) {
            lv.type = LiteralValue::STRING;
            lv.val = cur.text;
            advance();
        } else {
            throw std::runtime_error("Expected value but got '" + cur.text + "'");
        }
        return lv;
    }
};

// ── 公开 API ─────────────────────────────────────

// ParseSQL: 对外暴露的解析入口
// 创建 Lexer 和 Parser，解析 SQL 字符串并返回 AST
SQLStatement ParseSQL(const std::string &sql) {
    Lexer lex(sql);
    Parser parser(lex);
    return parser.parse();
}

} // namespace minidb
