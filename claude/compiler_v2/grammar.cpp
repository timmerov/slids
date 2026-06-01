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
            error(std::string("Expected '") + name + "'.");
            return false;
        }
        advance();
        return true;
    }

    bool isTypeStart(token::Kind k) const {
        return primitiveNameFor(k) != nullptr;
    }

    // Build a parse node stamped with explicit (file_id, tok).
    std::unique_ptr<parse::Node> newNodeAt(parse::Kind kind, int file_id, int tok) {
        auto n = std::make_unique<parse::Node>();
        n->kind = kind;
        n->file_id = file_id;
        n->tok = tok;
        return n;
    }

    // Build a parse node stamped at the current position.
    std::unique_ptr<parse::Node> newNodeHere(parse::Kind kind) {
        return newNodeAt(kind, peek().file_id, pos);
    }

    std::string parseType() {
        std::string type;
        if (char const* name = primitiveNameFor(peek().kind)) {
            type = name;
            advance();
        } else if (peek().kind == token::Kind::kIdentifier
                   || peek().kind == token::Kind::kColonColon) {
            // An identifier type name (alias / class / enum), possibly qualified
            // (`Space:Dir` — a type that is a namespace member). Joined with ':'
            // into one spelling; resolve walks the chain, validates, and
            // substitutes to the underlying before any downstream stage.
            std::vector<std::string> segs;
            std::vector<int> toks;
            bool global = false;
            if (!parseQualifiedName(segs, toks, global)) return "";
            if (global) type = "::";
            for (std::size_t i = 0; i < segs.size(); ++i) {
                if (i > 0) type += ":";
                type += segs[i];
            }
        } else {
            error("Expected type.");
            return "";
        }
        if (peek().kind == token::Kind::kLBracket) {
            advance();
            if (!expect(token::Kind::kRBracket, "]")) return "";
            type += "[]";
        }
        return type;
    }

    // Pure lookahead: do the tokens from the current position form a qualified
    // name (`A`, `A:B`, `::A:B`) immediately followed by an identifier? That is
    // a qualified TYPE spelling preceding a variable name (`Space:Dir x`), as
    // opposed to a name leading a call / assignment (`Space:foo()`,
    // `Space:kX = 1`). Consumes nothing.
    bool looksLikeQualifiedTypedDecl() const {
        int o = 0;
        if (peekKind(o) == token::Kind::kColonColon) o++;
        if (peekKind(o) != token::Kind::kIdentifier) return false;
        o++;
        while (peekKind(o) == token::Kind::kColon) {
            if (peekKind(o + 1) != token::Kind::kIdentifier) return false;
            o += 2;
        }
        return peekKind(o) == token::Kind::kIdentifier;
    }

    // Parse a (possibly qualified) name at the current token. Fills `segments`
    // with each `:`-separated identifier and `toks` with each segment's token
    // index (for per-segment carets); sets `global` for a leading `::`.
    // `Space:Nested:kFour` -> {Space, Nested, kFour}; `::kBest` -> global, {kBest}.
    bool parseQualifiedName(std::vector<std::string>& segments,
                            std::vector<int>& toks, bool& global) {
        global = false;
        if (peek().kind == token::Kind::kColonColon) {
            global = true;
            advance();
        }
        if (peek().kind != token::Kind::kIdentifier) {
            error("Expected a name.");
            return false;
        }
        segments.push_back(peek().text);
        toks.push_back(pos);
        advance();
        while (peek().kind == token::Kind::kColon) {
            advance();
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected a name after ':'.");
                return false;
            }
            segments.push_back(peek().text);
            toks.push_back(pos);
            advance();
        }
        return true;
    }

    std::unique_ptr<parse::Node> parsePrimary() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kStringLiteral) {
            auto node = newNodeHere(parse::Kind::kStringLiteral);
            node->text = t.text;
            advance();
            // adjacent string-literal concat at the token level
            while (peek().kind == token::Kind::kStringLiteral) {
                node->text += peek().text;
                advance();
            }
            return node;
        }
        if (t.kind == token::Kind::kIntLiteral) {
            auto node = newNodeHere(parse::Kind::kIntLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kUintLiteral) {
            auto node = newNodeHere(parse::Kind::kUintLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kCharLiteral) {
            auto node = newNodeHere(parse::Kind::kCharLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kFloatLiteral) {
            auto node = newNodeHere(parse::Kind::kFloatLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kBoolLiteral) {
            auto node = newNodeHere(parse::Kind::kBoolLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kIdentifier
            || t.kind == token::Kind::kColonColon) {
            int name_file = t.file_id;
            int start_tok = pos;
            std::vector<std::string> segs;
            std::vector<int> toks;
            bool global = false;
            if (!parseQualifiedName(segs, toks, global)) return nullptr;
            auto node = newNodeAt(parse::Kind::kIdentExpr, name_file, start_tok);
            node->name = segs.back();
            node->name_tok = toks.back();
            segs.pop_back();
            toks.pop_back();
            node->qualifier = std::move(segs);
            node->qualifier_toks = std::move(toks);
            node->global_qualified = global;
            return node;
        }
        if (t.kind == token::Kind::kLParen) {
            advance();
            auto inner = parseExpr();
            if (!inner) return nullptr;
            if (!expect(token::Kind::kRParen, ")")) return nullptr;
            return inner;
        }
        error("Expected expression.");
        return nullptr;
    }

    // Field access, indexing, postfix-^/^^, postfix-++/-- all slot in here as
    // their phases land. Today: postfix-call on a bare identifier.
    std::unique_ptr<parse::Node> parsePostfix(std::unique_ptr<parse::Node> base) {
        if (base->kind == parse::Kind::kIdentExpr
            && peek().kind == token::Kind::kLParen) {
            auto node = newNodeAt(parse::Kind::kCallExpr, base->file_id, base->tok);
            node->name = std::move(base->name);
            node->name_tok = base->name_tok;
            node->qualifier = std::move(base->qualifier);
            node->qualifier_toks = std::move(base->qualifier_toks);
            node->global_qualified = base->global_qualified;
            advance();   // (
            if (!parseCallArgs(*node)) return nullptr;
            base = std::move(node);
        }
        if (peek().kind == token::Kind::kPlusPlus
            || peek().kind == token::Kind::kMinusMinus) {
            char const* op = peek().kind == token::Kind::kPlusPlus ? "++" : "--";
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto node = newNodeAt(parse::Kind::kPostIncExpr, op_file, op_tok);
            node->text = op;
            node->children.push_back(std::move(base));
            return node;
        }
        return base;
    }

    std::unique_ptr<parse::Node> parseUnary() {
        token::Kind k = peek().kind;
        if (k == token::Kind::kPlusPlus || k == token::Kind::kMinusMinus) {
            char const* pp = k == token::Kind::kPlusPlus ? "++" : "--";
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = newNodeAt(parse::Kind::kPreIncExpr, op_file, op_tok);
            node->text = pp;
            node->children.push_back(std::move(operand));
            return node;
        }
        char const* op = nullptr;
        if      (k == token::Kind::kPlus)   op = "+";
        else if (k == token::Kind::kMinus)  op = "-";
        else if (k == token::Kind::kNot)    op = "!";
        else if (k == token::Kind::kBitNot) op = "~";
        if (op) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = newNodeAt(parse::Kind::kUnaryExpr, op_file, op_tok);
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
                                            std::unique_ptr<parse::Node> rhs,
                                            int op_file, int op_tok) {
        auto node = newNodeAt(parse::Kind::kBinaryExpr, op_file, op_tok);
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
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseUnary();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseAddSub() {
        auto left = parseMulDiv();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kPlus
            || peek().kind == token::Kind::kMinus) {
            std::string op = peek().text;
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseMulDiv();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseShift() {
        auto left = parseAddSub();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kLShift
            || peek().kind == token::Kind::kRShift) {
            std::string op = peek().text;
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseAddSub();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
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
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseShift();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseEquality() {
        auto left = parseRelational();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kEqEq
            || peek().kind == token::Kind::kNotEq) {
            std::string op = peek().text;
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseRelational();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitAnd() {
        auto left = parseEquality();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitAnd) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseEquality();
            if (!right) return nullptr;
            left = makeBinary("&", std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitXor() {
        auto left = parseBitAnd();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitXor) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseBitAnd();
            if (!right) return nullptr;
            left = makeBinary("^", std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitOr() {
        auto left = parseBitXor();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitOr) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseBitXor();
            if (!right) return nullptr;
            left = makeBinary("|", std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseLogicalAnd() {
        auto left = parseBitOr();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kAnd) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseBitOr();
            if (!right) return nullptr;
            left = makeBinary("&&", std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseExpr() {
        auto left = parseLogicalAnd();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kOr
            || peek().kind == token::Kind::kXorXor) {
            std::string op = (peek().kind == token::Kind::kOr) ? "||" : "^^";
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseLogicalAnd();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseVarDeclStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        bool is_const = false;
        if (peek().kind == token::Kind::kConst) {
            is_const = true;
            advance();
        }
        std::string type = parseType();
        if (fatal) return nullptr;
        if (peek().kind != token::Kind::kIdentifier
            && peek().kind != token::Kind::kColonColon) {
            error("Expected variable name.");
            return nullptr;
        }
        // The declared name may be qualified: `const int Space:kSix = 6;`
        // defines a member of an existing namespace by qualified name.
        std::vector<std::string> segs;
        std::vector<int> toks;
        bool global = false;
        if (!parseQualifiedName(segs, toks, global)) return nullptr;
        auto node = newNodeAt(parse::Kind::kVarDeclStmt, stmt_file, stmt_tok);
        node->name = segs.back();
        node->name_tok = toks.back();
        segs.pop_back();
        toks.pop_back();
        node->qualifier = std::move(segs);
        node->qualifier_toks = std::move(toks);
        node->global_qualified = global;
        node->return_type = std::move(type);
        node->is_const = is_const;
        if (peek().kind == token::Kind::kEquals) {
            advance();
            auto init = parseExpr();
            if (!init) return nullptr;
            node->children.push_back(std::move(init));
        } else if (is_const) {
            error("Constant declaration requires an initializer.");
            return nullptr;
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // A statement led by a (possibly qualified) name: `name = expr;`,
    // `name op= expr;`, or `name(args);`. The name may be qualified
    // (`Space:foo()`, `Space:kX = 1`) or carry a leading `::`. The single parser
    // for all three forms — qualified and bare alike — so the qualifier fields
    // ride through to resolve identically wherever a name leads a statement.
    std::unique_ptr<parse::Node> parseNameLedStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        std::vector<std::string> segs;
        std::vector<int> toks;
        bool global = false;
        if (!parseQualifiedName(segs, toks, global)) return nullptr;
        std::string name = segs.back();
        int name_tok = toks.back();
        segs.pop_back();
        toks.pop_back();

        auto stamp = [&](parse::Node& n) {
            n.name = std::move(name);
            n.name_tok = name_tok;
            n.qualifier = std::move(segs);
            n.qualifier_toks = std::move(toks);
            n.global_qualified = global;
        };

        token::Kind next = peek().kind;
        if (next == token::Kind::kLParen) {
            advance();   // (
            auto node = newNodeAt(parse::Kind::kCallStmt, stmt_file, stmt_tok);
            stamp(*node);
            if (!parseCallArgs(*node)) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            return node;
        }
        if (next == token::Kind::kEquals) {
            advance();   // =
            auto expr = parseExpr();
            if (!expr) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kAssignStmt, stmt_file, stmt_tok);
            stamp(*node);
            node->children.push_back(std::move(expr));
            return node;
        }
        if (char const* op = augAssignOp(next)) {
            advance();   // op=
            auto expr = parseExpr();
            if (!expr) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kAugAssignStmt, stmt_file, stmt_tok);
            stamp(*node);
            node->text = op;
            node->children.push_back(std::move(expr));
            return node;
        }
        error("Expected '=' or '(' after a name.");
        return nullptr;
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

    // Parses arguments into node->children, starting just past the '(' and
    // consuming the closing ')'. Shared by statement-form (parseNameLedStmt) and
    // expression-form (parsePostfix) calls. Returns false on error.
    bool parseCallArgs(parse::Node& node) {
        while (peek().kind != token::Kind::kRParen) {
            auto arg = parseExpr();
            if (!arg) return false;
            node.children.push_back(std::move(arg));
            if (peek().kind == token::Kind::kComma) {
                advance();
                continue;
            }
            if (peek().kind != token::Kind::kRParen) {
                error("Expected ',' or ')' in argument list.");
                return false;
            }
        }
        return expect(token::Kind::kRParen, ")");
    }

    std::unique_ptr<parse::Node> parseReturnStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // return
        auto expr = parseExpr();
        if (!expr) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = newNodeAt(parse::Kind::kReturnStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(expr));
        return node;
    }

    // A bare inc/dec statement (`++e;` `--e;` `e++;` `e--;`). Build the
    // inc/dec expression and wrap it in an expression-statement; resolve /
    // classify check the operand like any inc/dec, and desugar lowers it to a
    // bump (the discarded value needs no read).
    std::unique_ptr<parse::Node> parseIncDecStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        bool prefix = peek().kind == token::Kind::kPlusPlus
                   || peek().kind == token::Kind::kMinusMinus;
        token::Kind opk;
        int op_tok;
        std::string name;
        int name_tok;
        if (prefix) {
            opk = peek().kind;
            op_tok = pos;
            advance();   // ++ / --
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected a variable after '"
                      + std::string(opk == token::Kind::kPlusPlus ? "++" : "--")
                      + "'.");
                return nullptr;
            }
            name = peek().text;
            name_tok = pos;
            advance();
        } else {
            name = peek().text;
            name_tok = pos;
            advance();   // ident
            opk = peek().kind;
            op_tok = pos;
            advance();   // ++ / --
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto operand = newNodeAt(parse::Kind::kIdentExpr, stmt_file, name_tok);
        operand->name = std::move(name);
        auto inc = newNodeAt(prefix ? parse::Kind::kPreIncExpr
                                    : parse::Kind::kPostIncExpr,
                             stmt_file, op_tok);
        inc->text = opk == token::Kind::kPlusPlus ? "++" : "--";
        inc->children.push_back(std::move(operand));
        auto stmt = newNodeAt(parse::Kind::kExprStmt, stmt_file, stmt_tok);
        stmt->children.push_back(std::move(inc));
        return stmt;
    }

    // Two forms:
    //   value:  alias Name = Type;   (return_type = target spelling)
    //   bare:   alias Ns;            (return_type empty; qualifier/global mark the
    //                                 namespace whose members to import unqualified)
    std::unique_ptr<parse::Node> parseAliasDecl() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // alias
        if (peek().kind != token::Kind::kIdentifier
            && peek().kind != token::Kind::kColonColon) {
            error("Expected an alias name after 'alias'.");
            return nullptr;
        }
        std::vector<std::string> segs;
        std::vector<int> toks;
        bool global = false;
        if (!parseQualifiedName(segs, toks, global)) return nullptr;
        auto node = newNodeAt(parse::Kind::kAliasDecl, stmt_file, stmt_tok);
        node->name = segs.back();
        node->name_tok = toks.back();
        segs.pop_back();
        toks.pop_back();
        node->qualifier = std::move(segs);
        node->qualifier_toks = std::move(toks);
        node->global_qualified = global;
        if (peek().kind == token::Kind::kEquals) {
            advance();   // =
            std::string target = parseType();
            if (fatal) return nullptr;
            node->return_type = std::move(target);
        }
        // else: bare import form — return_type left empty.
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // enum [type] [Name] ( m1 [= expr], m2, ... );
    //   type-first, optional, default int. Name optional: present -> a named
    //   enum (alias + namespace of typed consts); absent -> anonymous (consts
    //   land in the enclosing scope). Each member is a const-shaped decl so it
    //   rides the existing const machinery; resolve fills auto-increment values.
    std::unique_ptr<parse::Node> parseEnumDecl() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // enum
        auto node = newNodeAt(parse::Kind::kEnumDecl, stmt_file, stmt_tok);
        // Optional underlying type. A type precedes the name / `(`; it is a
        // primitive keyword, or an identifier that is NOT immediately the enum
        // name followed by `(` and NOT the `(` itself. Distinguish structurally:
        //   enum (             -> anonymous, default int
        //   enum Name (        -> named, default int
        //   enum type (        -> anonymous, explicit type
        //   enum type Name (   -> named, explicit type
        std::string underlying = "int";
        if (isTypeStart(peek().kind)) {
            underlying = parseType();
            if (fatal) return nullptr;
        } else if (peek().kind == token::Kind::kIdentifier
                   && peekKind(1) == token::Kind::kIdentifier) {
            // `ident ident (` -> first ident is an (identifier) type spelling.
            underlying = parseType();
            if (fatal) return nullptr;
        }
        node->return_type = underlying;
        // Optional name.
        if (peek().kind == token::Kind::kIdentifier) {
            node->name = peek().text;
            node->name_tok = pos;
            advance();
        }
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        // Member list: ident [= expr], comma-separated.
        while (peek().kind != token::Kind::kRParen) {
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected an enum member name.");
                return nullptr;
            }
            auto m = newNodeAt(parse::Kind::kVarDeclStmt, peek().file_id, pos);
            m->name = peek().text;
            m->name_tok = pos;
            m->return_type = underlying;
            m->is_const = true;
            advance();
            if (peek().kind == token::Kind::kEquals) {
                advance();   // =
                auto init = parseExpr();
                if (!init) return nullptr;
                m->children.push_back(std::move(init));
            }
            node->children.push_back(std::move(m));
            if (peek().kind == token::Kind::kComma) {
                advance();
                continue;
            }
            if (peek().kind != token::Kind::kRParen) {
                error("Expected ',' or ')' in enum member list.");
                return nullptr;
            }
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // A namespace member: const, nested namespace, enum, or member function.
    std::unique_ptr<parse::Node> parseNamespaceMember() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kConst) return parseVarDeclStmt();
        if (t.kind == token::Kind::kAlias) return parseAliasDecl();
        if (t.kind == token::Kind::kEnum) return parseEnumDecl();
        if (t.kind == token::Kind::kIdentifier
            && peekKind(1) == token::Kind::kLBrace) return parseNamespaceDecl();
        return parseFunctionDef();
    }

    // Name { members } — open or reopen a namespace. No trailing semicolon.
    std::unique_ptr<parse::Node> parseNamespaceDecl() {
        int ns_file = peek().file_id;
        int ns_tok = pos;
        std::string name = peek().text;
        int name_tok = pos;
        advance();   // name
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;
        auto node = newNodeAt(parse::Kind::kNamespaceDecl, ns_file, ns_tok);
        node->name = std::move(name);
        node->name_tok = name_tok;
        while (!fatal && peek().kind != token::Kind::kRBrace) {
            if (peek().kind == token::Kind::kEndOfFile
                || peek().kind == token::Kind::kEndOfInput) {
                error("Expected '}'.");
                return nullptr;
            }
            auto m = parseNamespaceMember();
            if (!m) return nullptr;
            node->children.push_back(std::move(m));
        }
        if (!expect(token::Kind::kRBrace, "}")) return nullptr;
        return node;
    }

    // { stmts } — a nested lexical scope. A bare `{` (no leading ident) is a
    // block; `ident {` is a namespace decl, dispatched separately in parseStmt.
    std::unique_ptr<parse::Node> parseBlock() {
        int blk_file = peek().file_id;
        int blk_tok = pos;
        advance();   // {
        auto node = newNodeAt(parse::Kind::kBlockStmt, blk_file, blk_tok);
        while (!fatal && peek().kind != token::Kind::kRBrace) {
            if (peek().kind == token::Kind::kEndOfFile
                || peek().kind == token::Kind::kEndOfInput) {
                error("Expected '}'.");
                return nullptr;
            }
            auto stmt = parseStmt();
            if (!stmt) return nullptr;
            node->children.push_back(std::move(stmt));
        }
        if (!expect(token::Kind::kRBrace, "}")) return nullptr;
        return node;
    }

    // if ( cond ) { then } [ else { else } | else if ... ]. Both branches are
    // blocks (a nested scope, like every flow body); `else if` recurses so the
    // else-branch is another kIfStmt. children[0] = condition, [1] = then-block,
    // [2] = optional else-branch.
    std::unique_ptr<parse::Node> parseIfStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // if
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        auto cond = parseExpr();
        if (!cond) return nullptr;
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' after if condition.");
            return nullptr;
        }
        auto then_blk = parseBlock();
        if (!then_blk) return nullptr;
        auto node = newNodeAt(parse::Kind::kIfStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(then_blk));
        if (peek().kind == token::Kind::kElse) {
            advance();   // else
            std::unique_ptr<parse::Node> else_branch;
            if (peek().kind == token::Kind::kIf) {
                else_branch = parseIfStmt();
            } else if (peek().kind == token::Kind::kLBrace) {
                else_branch = parseBlock();
            } else {
                error("Expected '{' or 'if' after 'else'.");
                return nullptr;
            }
            if (!else_branch) return nullptr;
            node->children.push_back(std::move(else_branch));
        }
        return node;
    }

    // Parse `( [cond] )`. An empty condition is the always-true literal (a slids
    // convention); numeric already ran, so the post-numeric canonical text "1"
    // is synthesized directly. Returns the condition node, nullptr on error.
    std::unique_ptr<parse::Node> parseParenCondition() {
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        std::unique_ptr<parse::Node> cond;
        if (peek().kind == token::Kind::kRParen) {
            cond = newNodeHere(parse::Kind::kBoolLiteral);
            cond->text = "1";
        } else {
            cond = parseExpr();
            if (!cond) return nullptr;
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        return cond;
    }

    // Pre-condition  `while ( cond ) { body }`  -> kWhileStmt.
    // Post-condition `while { body } ( cond ) ;` -> kDoWhileStmt (body runs once).
    // Dispatched on the token after `while`: '{' opens the post-condition body,
    // anything else begins the pre-condition '(' clause. Both nodes store
    // children[0] = condition, [1] = body-block (a nested scope).
    std::unique_ptr<parse::Node> parseWhileStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // while
        if (peek().kind == token::Kind::kLBrace) {
            auto body = parseBlock();
            if (!body) return nullptr;
            auto cond = parseParenCondition();
            if (!cond) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kDoWhileStmt, stmt_file, stmt_tok);
            node->children.push_back(std::move(cond));
            node->children.push_back(std::move(body));
            return node;
        }
        auto cond = parseParenCondition();
        if (!cond) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' after while condition.");
            return nullptr;
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        auto node = newNodeAt(parse::Kind::kWhileStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(body));
        return node;
    }

    // break; / continue; — a bare keyword statement.
    std::unique_ptr<parse::Node> parseBreakContinue(parse::Kind kind) {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // break / continue
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return newNodeAt(kind, stmt_file, stmt_tok);
    }

    std::unique_ptr<parse::Node> parseStmt() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kLBrace) return parseBlock();
        if (t.kind == token::Kind::kIf) return parseIfStmt();
        if (t.kind == token::Kind::kWhile) return parseWhileStmt();
        if (t.kind == token::Kind::kBreak)
            return parseBreakContinue(parse::Kind::kBreakStmt);
        if (t.kind == token::Kind::kContinue)
            return parseBreakContinue(parse::Kind::kContinueStmt);
        if (t.kind == token::Kind::kReturn) return parseReturnStmt();
        if (t.kind == token::Kind::kConst) return parseVarDeclStmt();
        if (t.kind == token::Kind::kAlias) return parseAliasDecl();
        if (t.kind == token::Kind::kEnum) return parseEnumDecl();
        if (isTypeStart(t.kind)) return parseVarDeclStmt();
        if (t.kind == token::Kind::kPlusPlus
            || t.kind == token::Kind::kMinusMinus) return parseIncDecStmt();
        if (t.kind == token::Kind::kIdentifier) {
            token::Kind next = peekKind(1);
            if (next == token::Kind::kLBrace) return parseNamespaceDecl();
            if (next == token::Kind::kPlusPlus
                || next == token::Kind::kMinusMinus) return parseIncDecStmt();
            // `<ident> <ident>` is a declaration with an identifier type
            // (alias / class / enum), e.g. `Integer x = 42;`.
            if (next == token::Kind::kIdentifier) return parseVarDeclStmt();
            // `Space:Dir x` — a qualified type spelling preceding a var name.
            if (next == token::Kind::kColon
                && looksLikeQualifiedTypedDecl()) return parseVarDeclStmt();
            // ident, `ident:...` (qualified) -> assign / aug-assign / call.
            return parseNameLedStmt();
        }
        // A leading `::` is a global-qualified name: a typed decl (`::A:T x`) or
        // a name leading a call / assign.
        if (t.kind == token::Kind::kColonColon) {
            if (looksLikeQualifiedTypedDecl()) return parseVarDeclStmt();
            return parseNameLedStmt();
        }
        error("Expected statement.");
        return nullptr;
    }

    std::unique_ptr<parse::Node> parseFunctionDef() {
        int fn_file = peek().file_id;
        int fn_tok = pos;
        std::string ret_type = parseType();
        if (fatal) return nullptr;
        if (peek().kind != token::Kind::kIdentifier) {
            error("Expected function name.");
            return nullptr;
        }
        std::string name = peek().text;
        int name_tok = pos;
        advance();
        if (!expect(token::Kind::kLParen, "(")) return nullptr;

        auto node = newNodeAt(parse::Kind::kFunctionDef, fn_file, fn_tok);
        node->name = std::move(name);
        node->name_tok = name_tok;
        node->return_type = std::move(ret_type);

        while (peek().kind != token::Kind::kRParen) {
            int p_file = peek().file_id;
            int p_tok = pos;
            std::string p_type = parseType();
            if (fatal) return nullptr;
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected parameter name.");
                return nullptr;
            }
            std::string p_name = peek().text;
            int p_name_tok = pos;
            advance();
            auto p = newNodeAt(parse::Kind::kParam, p_file, p_tok);
            p->name = std::move(p_name);
            p->name_tok = p_name_tok;
            p->return_type = std::move(p_type);
            node->params.push_back(std::move(p));
            if (peek().kind == token::Kind::kComma) {
                advance();
                continue;
            }
            if (peek().kind != token::Kind::kRParen) {
                error("Expected ',' or ')' in parameter list.");
                return nullptr;
            }
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;

        if (peek().kind == token::Kind::kSemicolon) {
            advance();
            node->kind = parse::Kind::kFunctionDecl;
            return node;
        }
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;

        while (!fatal && peek().kind != token::Kind::kRBrace) {
            if (peek().kind == token::Kind::kEndOfFile
                || peek().kind == token::Kind::kEndOfInput) {
                error("Expected '}'.");
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
            // Top-level dispatch: `const` -> file-scope const decl; else
            // assume function def/decl.
            std::unique_ptr<parse::Node> child;
            if (peek().kind == token::Kind::kConst) {
                child = parseVarDeclStmt();
            } else if (peek().kind == token::Kind::kAlias) {
                child = parseAliasDecl();
            } else if (peek().kind == token::Kind::kEnum) {
                child = parseEnumDecl();
            } else if (peek().kind == token::Kind::kIdentifier
                       && peekKind(1) == token::Kind::kLBrace) {
                child = parseNamespaceDecl();
            } else {
                child = parseFunctionDef();
            }
            if (!child) return;
            prog->children.push_back(std::move(child));
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
