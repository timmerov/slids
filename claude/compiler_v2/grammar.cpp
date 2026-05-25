#include "grammar.h"

#include <memory>
#include <string>
#include <utility>

#include "diagnostic.h"
#include "parse.h"
#include "token.h"

namespace grammar {

namespace {

struct PrimitiveSpelling {
    token::Kind kind;
    char const* name;
};

constexpr PrimitiveSpelling kPrimitives[] = {
    {token::Kind::kBool,    "bool"},
    {token::Kind::kChar,    "char"},
    {token::Kind::kInt,     "int"},
    {token::Kind::kInt8,    "int8"},
    {token::Kind::kInt16,   "int16"},
    {token::Kind::kInt32,   "int32"},
    {token::Kind::kInt64,   "int64"},
    {token::Kind::kUint,    "uint"},
    {token::Kind::kUint8,   "uint8"},
    {token::Kind::kUint16,  "uint16"},
    {token::Kind::kUint32,  "uint32"},
    {token::Kind::kUint64,  "uint64"},
    {token::Kind::kIntptr,  "intptr"},
    {token::Kind::kFloat,   "float"},
    {token::Kind::kFloat32, "float32"},
    {token::Kind::kFloat64, "float64"},
    {token::Kind::kVoid,    "void"},
};

char const* primitiveNameFor(token::Kind k) {
    for (auto const& m : kPrimitives) {
        if (m.kind == k) return m.name;
    }
    return nullptr;
}

struct Parser {
    token::List const& tokens;
    parse::Tree& out;
    diagnostic::Sink& diag;
    int pos = 0;
    bool fatal = false;

    token::Token const& peek() const { return tokens.tokens[pos]; }

    token::Kind peekKind(int ahead) const {
        int p = pos + ahead;
        if (p >= static_cast<int>(tokens.tokens.size())) return token::Kind::kEndOfInput;
        return tokens.tokens[p].kind;
    }

    void advance() {
        if (pos + 1 < static_cast<int>(tokens.tokens.size())) pos++;
    }

    void error(std::string const& msg) {
        if (fatal) return;
        fatal = true;
        token::Token const& t = peek();
        diagnostic::report(diag, {t.file_id, pos, msg, {}});
    }

    bool expect(token::Kind kind, char const* name) {
        if (peek().kind != kind) {
            error(std::string("expected '") + name + "'");
            return false;
        }
        advance();
        return true;
    }

    bool isTypeStart(token::Kind k) const {
        return primitiveNameFor(k) != nullptr;
    }

    std::string parseType() {
        char const* name = primitiveNameFor(peek().kind);
        if (!name) {
            error("expected type");
            return "";
        }
        std::string type = name;
        advance();
        if (peek().kind == token::Kind::kLBracket) {
            advance();
            if (!expect(token::Kind::kRBracket, "]")) return "";
            type += "[]";
        }
        return type;
    }

    std::unique_ptr<parse::Node> parsePrimary() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kStringLiteral) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kStringLiteral;
            node->text = t.text;
            advance();
            // adjacent string-literal concat at the token level
            while (peek().kind == token::Kind::kStringLiteral) {
                node->text += peek().text;
                advance();
            }
            return node;
        }
        if (t.kind == token::Kind::kIntLiteral
            || t.kind == token::Kind::kUintLiteral) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kIntLiteral;
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kCharLiteral) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kCharLiteral;
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kFloatLiteral) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kFloatLiteral;
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kTrue || t.kind == token::Kind::kFalse) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kBoolLiteral;
            node->text = (t.kind == token::Kind::kTrue) ? "true" : "false";
            advance();
            return node;
        }
        if (t.kind == token::Kind::kIdentifier) {
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kIdentExpr;
            node->name = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kLParen) {
            advance();
            auto inner = parseExpr();
            if (!inner) return nullptr;
            if (!expect(token::Kind::kRParen, ")")) return nullptr;
            return inner;
        }
        error("expected expression");
        return nullptr;
    }

    // Passthrough today. Field access, indexing, postfix-call, postfix-^/^^,
    // postfix-++/-- all slot in here as their phases land.
    std::unique_ptr<parse::Node> parsePostfix(std::unique_ptr<parse::Node> base) {
        return base;
    }

    std::unique_ptr<parse::Node> parseUnary() {
        token::Kind k = peek().kind;
        char const* op = nullptr;
        if      (k == token::Kind::kPlus)   op = "+";
        else if (k == token::Kind::kMinus)  op = "-";
        else if (k == token::Kind::kNot)    op = "!";
        else if (k == token::Kind::kBitNot) op = "~";
        if (op) {
            advance();
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = std::make_unique<parse::Node>();
            node->kind = parse::Kind::kUnaryExpr;
            node->text = op;
            node->children.push_back(std::move(operand));
            return node;
        }
        auto prim = parsePrimary();
        if (!prim) return nullptr;
        return parsePostfix(std::move(prim));
    }

    std::unique_ptr<parse::Node> makeBinary(std::string op,
                                            std::unique_ptr<parse::Node> lhs,
                                            std::unique_ptr<parse::Node> rhs) {
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kBinaryExpr;
        node->text = std::move(op);
        node->children.push_back(std::move(lhs));
        node->children.push_back(std::move(rhs));
        return node;
    }

    std::unique_ptr<parse::Node> parseMulDiv() {
        auto left = parseUnary();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kStar
            || peek().kind == token::Kind::kSlash
            || peek().kind == token::Kind::kPercent) {
            std::string op = peek().text;
            advance();
            auto right = parseUnary();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseAddSub() {
        auto left = parseMulDiv();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kPlus
            || peek().kind == token::Kind::kMinus) {
            std::string op = peek().text;
            advance();
            auto right = parseMulDiv();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseShift() {
        auto left = parseAddSub();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kLShift
            || peek().kind == token::Kind::kRShift) {
            std::string op = peek().text;
            advance();
            auto right = parseAddSub();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseRelational() {
        auto left = parseShift();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kLt
            || peek().kind == token::Kind::kGt
            || peek().kind == token::Kind::kLtEq
            || peek().kind == token::Kind::kGtEq) {
            std::string op = peek().text;
            advance();
            auto right = parseShift();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseEquality() {
        auto left = parseRelational();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kEqEq
            || peek().kind == token::Kind::kNotEq) {
            std::string op = peek().text;
            advance();
            auto right = parseRelational();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitAnd() {
        auto left = parseEquality();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitAnd) {
            advance();
            auto right = parseEquality();
            if (!right) return nullptr;
            left = makeBinary("&", std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitXor() {
        auto left = parseBitAnd();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitXor) {
            advance();
            auto right = parseBitAnd();
            if (!right) return nullptr;
            left = makeBinary("^", std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitOr() {
        auto left = parseBitXor();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitOr) {
            advance();
            auto right = parseBitXor();
            if (!right) return nullptr;
            left = makeBinary("|", std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseLogicalAnd() {
        auto left = parseBitOr();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kAnd) {
            advance();
            auto right = parseBitOr();
            if (!right) return nullptr;
            left = makeBinary("&&", std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseExpr() {
        auto left = parseLogicalAnd();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kOr
            || peek().kind == token::Kind::kXorXor) {
            std::string op = (peek().kind == token::Kind::kOr) ? "||" : "^^";
            advance();
            auto right = parseLogicalAnd();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right));
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseVarDeclStmt() {
        std::string type = parseType();
        if (fatal) return nullptr;
        if (peek().kind != token::Kind::kIdentifier) {
            error("expected variable name");
            return nullptr;
        }
        std::string name = peek().text;
        advance();
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kVarDeclStmt;
        node->name = std::move(name);
        node->return_type = std::move(type);
        if (peek().kind == token::Kind::kEquals) {
            advance();
            auto init = parseExpr();
            if (!init) return nullptr;
            node->children.push_back(std::move(init));
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    std::unique_ptr<parse::Node> parseAssignStmt() {
        std::string name = peek().text;
        advance();   // ident
        advance();   // =  (caller verified via lookahead)
        auto expr = parseExpr();
        if (!expr) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kAssignStmt;
        node->name = std::move(name);
        node->children.push_back(std::move(expr));
        return node;
    }

    // Map an augmented-assign token kind to its op string. Returns nullptr
    // for tokens that aren't augmented-assigns.
    static char const* augAssignOp(token::Kind k) {
        if (k == token::Kind::kPlusEq)    return "+";
        if (k == token::Kind::kMinusEq)   return "-";
        if (k == token::Kind::kStarEq)    return "*";
        if (k == token::Kind::kSlashEq)   return "/";
        if (k == token::Kind::kPercentEq) return "%";
        if (k == token::Kind::kBitAndEq)  return "&";
        if (k == token::Kind::kBitOrEq)   return "|";
        if (k == token::Kind::kBitXorEq)  return "^";
        if (k == token::Kind::kLShiftEq)  return "<<";
        if (k == token::Kind::kRShiftEq)  return ">>";
        if (k == token::Kind::kAndEq)     return "&&";
        if (k == token::Kind::kOrEq)      return "||";
        if (k == token::Kind::kXorXorEq)  return "^^";
        return nullptr;
    }

    std::unique_ptr<parse::Node> parseAugAssignStmt() {
        std::string name = peek().text;
        advance();   // ident
        char const* op = augAssignOp(peek().kind);
        advance();   // op= (caller verified via lookahead + augAssignOp)
        auto expr = parseExpr();
        if (!expr) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kAugAssignStmt;
        node->name = std::move(name);
        node->text = op;
        node->children.push_back(std::move(expr));
        return node;
    }

    std::unique_ptr<parse::Node> parseCallStmt() {
        std::string callee = peek().text;
        advance();   // ident
        advance();   // (  (caller verified via lookahead)
        auto arg = parseExpr();
        if (!arg) return nullptr;
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kCallStmt;
        node->name = std::move(callee);
        node->children.push_back(std::move(arg));
        return node;
    }

    std::unique_ptr<parse::Node> parseReturnStmt() {
        advance();   // return
        auto expr = parseExpr();
        if (!expr) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = std::make_unique<parse::Node>();
        node->kind = parse::Kind::kReturnStmt;
        node->children.push_back(std::move(expr));
        return node;
    }

    std::unique_ptr<parse::Node> parseStmt() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kReturn) return parseReturnStmt();
        if (isTypeStart(t.kind)) return parseVarDeclStmt();
        if (t.kind == token::Kind::kIdentifier) {
            token::Kind next = peekKind(1);
            if (next == token::Kind::kEquals) return parseAssignStmt();
            if (next == token::Kind::kLParen) return parseCallStmt();
            if (augAssignOp(next) != nullptr) return parseAugAssignStmt();
            error("expected '=' or '(' after identifier");
            return nullptr;
        }
        error("expected statement");
        return nullptr;
    }

    std::unique_ptr<parse::Node> parseFunctionDef() {
        std::string ret_type = parseType();
        if (fatal) return nullptr;
        if (peek().kind != token::Kind::kIdentifier) {
            error("expected function name");
            return nullptr;
        }
        std::string name = peek().text;
        advance();
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        if (!expect(token::Kind::kRParen, ")")) return nullptr;

        auto node = std::make_unique<parse::Node>();
        node->name = std::move(name);
        node->return_type = std::move(ret_type);

        if (peek().kind == token::Kind::kSemicolon) {
            advance();
            node->kind = parse::Kind::kFunctionDecl;
            return node;
        }
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;
        node->kind = parse::Kind::kFunctionDef;

        while (!fatal && peek().kind != token::Kind::kRBrace) {
            if (peek().kind == token::Kind::kEndOfFile
                || peek().kind == token::Kind::kEndOfInput) {
                error("expected '}'");
                return nullptr;
            }
            auto stmt = parseStmt();
            if (!stmt) return nullptr;
            node->children.push_back(std::move(stmt));
        }
        if (!expect(token::Kind::kRBrace, "}")) return nullptr;
        return node;
    }

    void parseProgram() {
        auto prog = std::make_unique<parse::Node>();
        prog->kind = parse::Kind::kProgram;
        while (!fatal) {
            while (peek().kind == token::Kind::kEndOfFile) advance();
            if (peek().kind == token::Kind::kEndOfInput) break;
            auto fn = parseFunctionDef();
            if (!fn) return;
            prog->children.push_back(std::move(fn));
        }
        if (!fatal) out.nodes.push_back(std::move(prog));
    }
};

}  // namespace

void run(token::List const& in, parse::Tree& out, diagnostic::Sink& diag) {
    Parser p{in, out, diag};
    p.parseProgram();
}

}  // namespace grammar
