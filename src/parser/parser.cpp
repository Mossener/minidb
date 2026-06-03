#include "parser/parser.h"
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace minidb {

// ── LiteralValue helpers ───────────────────────────

int64_t LiteralValue::AsInt() const {
    return std::stoll(val);
}

double LiteralValue::AsFloat() const {
    return std::stod(val);
}

std::string LiteralValue::AsString() const {
    return val;
}

ColType ColumnTypeFromString(const std::string &s) {
    std::string u;
    for (char c : s) u.push_back(static_cast<char>(std::toupper(c)));
    if (u == "INT" || u == "INTEGER") return ColType::INTEGER;
    if (u == "FLOAT" || u == "DOUBLE" || u == "REAL") return ColType::FLOAT;
    if (u == "VARCHAR" || u == "TEXT" || u == "STRING") return ColType::VARCHAR;
    if (u == "BOOLEAN" || u == "BOOL") return ColType::BOOLEAN;
    throw std::runtime_error("Unknown type: " + s);
}

// ── Token ─────────────────────────────────────────

enum class Tok {
    CREATE, TABLE, INSERT, INTO, VALUES, SELECT, FROM, DELETE,
    WHERE, AND, OR, BEGIN_TOK, COMMIT_TOK, ABORT_TOK, DROP, INDEX,
    IDENT, NUMBER, STRING, BOOL_LIT, STAR, LPAREN, RPAREN, COMMA, SEMI,
    EQ, NE, LT, GT, LE, GE, END, UNKNOWN
};

struct Token {
    Tok type;
    std::string text;
};

static std::string tok_lower(const std::string &s) {
    std::string r;
    for (char c : s) r.push_back(static_cast<char>(std::tolower(c)));
    return r;
}

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

// ── Lexer ─────────────────────────────────────────

class Lexer {
public:
    explicit Lexer(const std::string &s) : input(s), pos(0) {}

    Token next() {
        skip_ws();
        if (pos >= input.size()) return {Tok::END, ""};

        char c = input[pos];

        // -- comment
        if (c == '-' && pos + 1 < input.size() && input[pos + 1] == '-') {
            while (pos < input.size() && input[pos] != '\n') pos++;
            return next();
        }

        if (c == ';') { pos++; return {Tok::SEMI, ";"}; }
        if (c == '*') { pos++; return {Tok::STAR, "*"}; }
        if (c == '(') { pos++; return {Tok::LPAREN, "("}; }
        if (c == ')') { pos++; return {Tok::RPAREN, ")"}; }
        if (c == ',') { pos++; return {Tok::COMMA, ","}; }
        if (c == '=') { pos++; return {Tok::EQ, "="}; }

        if (c == '!' && pos + 1 < input.size() && input[pos + 1] == '=') {
            pos += 2; return {Tok::NE, "!="};
        }
        if (c == '<') {
            if (pos + 1 < input.size() && input[pos + 1] == '=') { pos += 2; return {Tok::LE, "<="}; }
            if (pos + 1 < input.size() && input[pos + 1] == '>') { pos += 2; return {Tok::NE, "<>"}; }
            pos++; return {Tok::LT, "<"};
        }
        if (c == '>') {
            if (pos + 1 < input.size() && input[pos + 1] == '=') { pos += 2; return {Tok::GE, ">="}; }
            pos++; return {Tok::GT, ">"};
        }

        // string literal
        if (c == '\'' || c == '"') {
            char quote = c;
            pos++;
            size_t start = pos;
            while (pos < input.size() && input[pos] != quote) {
                if (input[pos] == '\\') pos++; // skip escape
                pos++;
            }
            std::string text = input.substr(start, pos - start);
            if (pos < input.size()) pos++;
            return {Tok::STRING, text};
        }

        // number
        if (std::isdigit(c) || (c == '-' && pos + 1 < input.size() && std::isdigit(input[pos + 1]))) {
            size_t start = pos;
            if (c == '-') pos++;
            while (pos < input.size() && (std::isdigit(input[pos]) || input[pos] == '.')) pos++;
            return {Tok::NUMBER, input.substr(start, pos - start)};
        }

        // identifier or keyword
        if (std::isalpha(c) || c == '_') {
            size_t start = pos;
            while (pos < input.size() && (std::isalnum(input[pos]) || input[pos] == '_')) pos++;
            std::string word = input.substr(start, pos - start);
            std::string low = tok_lower(word);
            if (low == "true" || low == "false") return {Tok::BOOL_LIT, word};
            Tok kw = keyword_tok(word);
            return {kw, word};
        }

        pos++;
        return {Tok::UNKNOWN, std::string(1, c)};
    }

private:
    std::string input;
    size_t pos;

    void skip_ws() {
        while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' ||
               input[pos] == '\n' || input[pos] == '\r')) pos++;
    }
};

// ── Parser ─────────────────────────────────────────

class Parser {
public:
    explicit Parser(Lexer &l) : lex(l) { cur = lex.next(); }

    SQLStatement parse() {
        switch (cur.type) {
            case Tok::CREATE: return parse_create_table();
            case Tok::INSERT: return parse_insert();
            case Tok::SELECT: return parse_select();
            case Tok::DELETE: return parse_delete();
            case Tok::BEGIN_TOK: return parse_begin();
            case Tok::COMMIT_TOK: return parse_commit();
            case Tok::ABORT_TOK: return parse_abort();
            default:
                throw std::runtime_error("Unexpected token: '" + cur.text + "'");
        }
    }

private:
    Lexer &lex;
    Token cur;

    void advance() { cur = lex.next(); }

    void expect(Tok t) {
        if (cur.type != t)
            throw std::runtime_error("Syntax error: expected token " +
                std::to_string(static_cast<int>(t)) + " but got '" + cur.text + "'");
        advance();
    }

    // CREATE TABLE name ( col TYPE, ... )
    SQLStatement parse_create_table() {
        advance(); // skip CREATE
        expect(Tok::TABLE);
        SQLStatement stmt;
        stmt.type = SQLType::CREATE_TABLE;
        stmt.table_name = cur.text;
        expect(Tok::IDENT);
        expect(Tok::LPAREN);

        while (cur.type != Tok::RPAREN) {
            ColumnDef cd;
            cd.name = cur.text;
            expect(Tok::IDENT);
            cd.type = ColumnTypeFromString(cur.text);
            advance(); // eat the type keyword

            if (cd.type == ColType::VARCHAR && cur.type == Tok::LPAREN) {
                advance(); // eat '('
                cd.length = static_cast<size_t>(std::stoul(cur.text));
                expect(Tok::NUMBER);
                expect(Tok::RPAREN);
            } else if (cd.type == ColType::VARCHAR) {
                cd.length = 64; // default
            }

            stmt.columns.push_back(cd);

            if (cur.type == Tok::COMMA) advance();
        }
        expect(Tok::RPAREN);
        if (cur.type == Tok::SEMI) advance();
        return stmt;
    }

    // INSERT INTO table VALUES (val, ...) [, (val, ...)]
    SQLStatement parse_insert() {
        advance(); // skip INSERT
        expect(Tok::INTO);
        SQLStatement stmt;
        stmt.type = SQLType::INSERT;
        stmt.table_name = cur.text;
        expect(Tok::IDENT);
        expect(Tok::VALUES);

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

    // SELECT * FROM table [WHERE cond]
    SQLStatement parse_select() {
        advance(); // skip SELECT
        SQLStatement stmt;
        stmt.type = SQLType::SELECT;

        if (cur.type == Tok::STAR) {
            advance(); // skip *
        } else {
            // skip column list (not used currently)
            while (cur.type != Tok::FROM) advance();
        }

        expect(Tok::FROM);
        stmt.table_name = cur.text;
        expect(Tok::IDENT);

        if (cur.type == Tok::WHERE) {
            advance();
            stmt.where = parse_condition();
        }

        if (cur.type == Tok::SEMI) advance();
        return stmt;
    }

    // DELETE FROM table WHERE cond
    SQLStatement parse_delete() {
        advance(); // skip DELETE
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

    SQLStatement parse_begin() {
        advance();
        if (cur.type == Tok::SEMI) advance();
        SQLStatement stmt;
        stmt.type = SQLType::BEGIN_TXN;
        return stmt;
    }

    SQLStatement parse_commit() {
        advance();
        if (cur.type == Tok::SEMI) advance();
        SQLStatement stmt;
        stmt.type = SQLType::COMMIT_TXN;
        return stmt;
    }

    SQLStatement parse_abort() {
        advance();
        if (cur.type == Tok::SEMI) advance();
        SQLStatement stmt;
        stmt.type = SQLType::ABORT_TXN;
        return stmt;
    }

    // cond: column op literal
    std::unique_ptr<CompareCondition> parse_condition() {
        auto cond = std::make_unique<CompareCondition>();
        cond->column = cur.text;
        expect(Tok::IDENT);

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

    LiteralValue parse_literal() {
        LiteralValue lv;
        if (cur.type == Tok::NUMBER) {
            std::string t = cur.text;
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

// ── Public API ─────────────────────────────────────

SQLStatement ParseSQL(const std::string &sql) {
    Lexer lex(sql);
    Parser parser(lex);
    return parser.parse();
}

} // namespace minidb
