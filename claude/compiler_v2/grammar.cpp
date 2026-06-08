#include "grammar.h"

#include <ctime>
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
    bool case_label_ = false;        // parsing a switch case-label const-expr: a
                                     // qualified name's trailing `:` is the label
                                     // terminator, not a qualifier separator
    std::string current_func = {};   // enclosing function name, for ##func
    std::string clock_date = {};     // ##date / ##time text, captured once per
    std::string clock_time = {};     // compile (this .sl's compile, not slidsc's)
    bool clock_captured = false;

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

    // Report at an explicit token (caret precision when the offender is not the
    // current position — e.g. an already-consumed macro name).
    void errorAt(int tok, std::string const& msg) {
        if (fatal) return;
        fatal = true;
        diagnostic::report(diag, {tokens.tokens[tok].file_id, tok, msg, {}});
    }

    // ##date / ##time expand to the moment THIS .sl is compiled, captured once so
    // every macro in the file agrees. Format matches v1: "Mmm DD YYYY" (space-
    // padded day) and "HH:MM:SS".
    void captureClock() {
        if (clock_captured) return;
        clock_captured = true;
        std::time_t now = std::time(nullptr);
        std::tm tm_buf{};
        localtime_r(&now, &tm_buf);
        char date_buf[32] = {0};
        char time_buf[32] = {0};
        std::strftime(date_buf, sizeof(date_buf), "%b %e %Y", &tm_buf);
        std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_buf);
        clock_date = date_buf;
        clock_time = time_buf;
    }

    // Filename without its directory — ##file is the short name (no path).
    static std::string baseName(std::string const& path) {
        auto slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
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

    // `seg_toks` (optional out): for a qualified identifier type, the token index
    // of each `:`-separated segment, so resolve can caret the offending segment
    // (a flat spelling string otherwise loses them). Left empty for a primitive.
    std::string parseType(std::vector<int>* seg_toks = nullptr) {
        std::string type;
        if (peek().kind == token::Kind::kLParen) {
            // Anonymous tuple type `(T0, T1, ...)`. A size-1 `(T)` collapses to T
            // (the comma is the tuple marker). Elements recurse, so nested tuples
            // and qualified/aliased element types work.
            advance();   // (
            if (peek().kind == token::Kind::kRParen) {
                error("A tuple type needs at least one element.");
                return "";
            }
            std::vector<std::string> elems;
            while (true) {
                std::string e = parseType();
                if (e.empty()) return "";
                elems.push_back(e);
                if (peek().kind != token::Kind::kComma) break;
                advance();   // ,
            }
            if (!expect(token::Kind::kRParen, ")")) return "";
            if (elems.size() == 1) {
                type = elems[0];
            } else {
                type = "(";
                for (std::size_t i = 0; i < elems.size(); i++) {
                    if (i) type += ", ";
                    type += elems[i];
                }
                type += ")";
            }
        } else if (char const* name = primitiveNameFor(peek().kind)) {
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
            if (seg_toks) *seg_toks = std::move(toks);
        } else {
            error("Expected type.");
            return "";
        }
        // A pointer suffix: `T[]` (iterator) or `T^` (reference). Mutually
        // exclusive — a type carries at most one category modifier.
        if (peek().kind == token::Kind::kLBracket) {
            advance();
            if (!expect(token::Kind::kRBracket, "]")) return "";
            type += "[]";
        } else if (peek().kind == token::Kind::kBitXor) {
            advance();
            type += "^";
        }
        return type;
    }

    // Pure lookahead: do the tokens from the current position form a (qualified)
    // identifier TYPE spelling, with an optional pointer suffix, immediately
    // followed by an identifier? That is a typed var decl whose type is an
    // identifier type (alias / enum / class), possibly a reference or iterator:
    // `Space:Dir x`, `Integer^ ref`, `Integer[] iter`, `Space:Dir^ d`. Opposed to
    // a name leading a call / assignment (`Space:foo()`, `Space:kX = 1`), a deref
    // store (`p^ = v`), or an index store (`arr[i] = v` — a NON-empty `[i]`, so
    // the iterator suffix below doesn't match). Primitive-typed decls don't come
    // here (isTypeStart catches them). Consumes nothing.
    bool looksLikeQualifiedTypedDecl() const {
        int o = 0;
        if (peekKind(o) == token::Kind::kColonColon) o++;
        if (peekKind(o) != token::Kind::kIdentifier) return false;
        o++;
        while (peekKind(o) == token::Kind::kColon) {
            if (peekKind(o + 1) != token::Kind::kIdentifier) return false;
            o += 2;
        }
        // An optional reference (`^`) or iterator (`[]` — empty brackets only)
        // suffix, mirroring parseType. A non-empty `[i]` is a subscript, not a
        // type suffix, so it is left for the name-led store path.
        if (peekKind(o) == token::Kind::kBitXor) {
            o++;
        } else if (peekKind(o) == token::Kind::kLBracket
                   && peekKind(o + 1) == token::Kind::kRBracket) {
            o += 2;
        }
        return peekKind(o) == token::Kind::kIdentifier;
    }

    // Pure lookahead: does a leading `(...)` form a tuple-TYPE declaration —
    // `(T0, T1) name` — as opposed to a parenthesized / tuple-literal expression
    // statement? Scan to the matching `)`, skip an optional `^` / `[]` suffix,
    // and require a trailing identifier (the var name). Consumes nothing.
    bool looksLikeTupleTypeDecl() const {
        if (peekKind(0) != token::Kind::kLParen) return false;
        int o = 1, depth = 1;
        while (depth > 0) {
            token::Kind k = peekKind(o);
            if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput)
                return false;
            if (k == token::Kind::kLParen) depth++;
            else if (k == token::Kind::kRParen) depth--;
            o++;
        }
        if (peekKind(o) == token::Kind::kBitXor) o++;
        else if (peekKind(o) == token::Kind::kLBracket
                 && peekKind(o + 1) == token::Kind::kRBracket) o += 2;
        return peekKind(o) == token::Kind::kIdentifier;
    }

    // Pure lookahead: does a leading `(...)` form a tuple DESTRUCTURE assignment —
    // `(a, b, ) = tuple` — i.e. a `(...)` immediately followed by `=`? (Opposed to
    // a tuple-type decl, which has a trailing name, or a paren-expr statement.)
    bool looksLikeTupleDestructure() const {
        if (peekKind(0) != token::Kind::kLParen) return false;
        int o = 1, depth = 1;
        while (depth > 0) {
            token::Kind k = peekKind(o);
            if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput)
                return false;
            if (k == token::Kind::kLParen) depth++;
            else if (k == token::Kind::kRParen) depth--;
            o++;
        }
        return peekKind(o) == token::Kind::kEquals;
    }

    // Parse a for-varlist variable head: an optional type then the variable
    // name. The decl is TYPELESS (vtype left empty -> inferred from the
    // initializer, or a reuse of an enclosing local) when the leading tokens
    // are neither a primitive type-start nor a qualified typed-decl shape
    // (`Ident Ident` / `Ident:Ident... Ident`); then the leading identifier is
    // the variable name. Consumes the type (if any) and the name; never the
    // trailing ':' / '=' / ',' / ')'. Returns false on a diagnosed error.
    bool parseForVarHead(std::string& vtype, std::string& vname,
                         int& v_file, int& v_tok, int& vname_tok) {
        v_file = peek().file_id;
        v_tok = pos;
        if (isTypeStart(peek().kind) || looksLikeQualifiedTypedDecl()) {
            vtype = parseType();
            if (fatal) return false;
        } else {
            vtype.clear();   // typeless: the next identifier IS the name
        }
        if (peek().kind != token::Kind::kIdentifier) {
            error("Expected a variable name in the for-loop.");
            return false;
        }
        vname = peek().text;
        vname_tok = pos;
        advance();   // name
        return true;
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

    // A qualified name inside a switch case-label, where the chain's trailing `:`
    // is the label TERMINATOR, not a qualifier separator. Scans the maximal
    // `:`-ident chain; if it is NOT followed by the terminator `:`, the chain
    // over-consumed by one segment (the last ident starts the clause body), so
    // rewind one. This resolves the v1 `case Dir:N:` ambiguity for every
    // realistic body (keyword-, brace-, or identifier-led). Consumes through the
    // last label segment, leaving pos at the terminator `:`.
    bool parseQualifiedNameCaseLabel(std::vector<std::string>& segments,
                                     std::vector<int>& toks, bool& global) {
        global = false;
        if (peek().kind == token::Kind::kColonColon) { global = true; advance(); }
        if (peek().kind != token::Kind::kIdentifier) {
            error("Expected a name.");
            return false;
        }
        segments.push_back(peek().text);
        toks.push_back(pos);
        advance();
        while (peek().kind == token::Kind::kColon
               && peekKind(1) == token::Kind::kIdentifier) {
            advance();   // ':'
            segments.push_back(peek().text);
            toks.push_back(pos);
            advance();   // ident
        }
        // Over-consumed (no terminator `:` follows) -> the last segment is the
        // body's first identifier; give it back so the preceding `:` terminates.
        if (peek().kind != token::Kind::kColon && segments.size() >= 2) {
            pos -= 2;
            segments.pop_back();
            toks.pop_back();
        }
        return true;
    }

    // One `Type=operand` link of a value conversion. Called positioned at the
    // target type (the leading `(` already consumed; the trailing `)` is the
    // caller's). The operand is another link when it too begins with a type
    // keyword (`(float64 = intptr = ref)`), else a full expression.
    std::unique_ptr<parse::Node> parseConvertChain(int file_id) {
        int op_tok = pos;
        std::vector<int> target_seg_toks;
        std::string target = parseType(&target_seg_toks);
        if (target.empty()) return nullptr;
        if (!expect(token::Kind::kEquals, "=")) return nullptr;
        std::unique_ptr<parse::Node> operand =
            isTypeStart(peek().kind) ? parseConvertChain(file_id) : parseExpr();
        if (!operand) return nullptr;
        auto node = newNodeAt(parse::Kind::kConvertExpr, file_id, op_tok);
        node->return_type = widen::internOrNone(target);
        node->return_type_seg_toks = std::move(target_seg_toks);
        node->children.push_back(std::move(operand));
        return node;
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
        if (t.kind == token::Kind::kNullptr) {
            auto node = newNodeHere(parse::Kind::kNullptrLiteral);
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
            bool ok = case_label_
                ? parseQualifiedNameCaseLabel(segs, toks, global)
                : parseQualifiedName(segs, toks, global);
            if (!ok) return nullptr;
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
            int lp = pos;
            advance();
            // `(Type=expr)` value conversion. No parenthesized expression or
            // tuple literal can start with a type keyword, so a type-start right
            // after `(` unambiguously marks a conversion. Chains `(A=B=expr)`
            // nest right-to-left (one kConvertExpr per `Type=` link).
            if (isTypeStart(peek().kind)) {
                auto conv = parseConvertChain(t.file_id);
                if (!conv) return nullptr;
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
                return conv;
            }
            auto inner = parseExpr();
            if (!inner) return nullptr;
            if (peek().kind == token::Kind::kComma) {
                // Tuple literal `(e0, e1, ...)` — the comma is the marker; a
                // size-1 `(e)` is just a parenthesized expr (the branch below).
                auto tup = newNodeAt(parse::Kind::kTupleExpr, t.file_id, lp);
                tup->children.push_back(std::move(inner));
                while (peek().kind == token::Kind::kComma) {
                    advance();   // ,
                    auto e = parseExpr();
                    if (!e) return nullptr;
                    tup->children.push_back(std::move(e));
                }
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
                return tup;
            }
            if (!expect(token::Kind::kRParen, ")")) return nullptr;
            return inner;
        }
        if (t.kind == token::Kind::kNew) {
            // new T            -> a single object (T^)
            // new T[n]         -> an array of n objects (T[])
            // new(addr) T[n]   -> placement: construct at addr, no allocation
            // children[0] = array-size expr (or null), [1] = placement-addr (or
            // null). Phase 4: primitives only — no constructor args yet (the
            // `new T(value)` initializer form needs tuples).
            int new_file = t.file_id;
            int new_tok = pos;
            advance();   // new
            std::unique_ptr<parse::Node> addr;
            // A `(` here is a placement address. TODO: once paren-led type
            // spellings exist (anonymous tuples `(T1,T2)`, const-pointer
            // `(const T)^`), this needs a placement-vs-type lookahead; today no
            // type starts with `(`, so `(` unambiguously opens a placement addr.
            if (peek().kind == token::Kind::kLParen) {
                advance();   // (
                addr = parseExpr();
                if (!addr) return nullptr;
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
            }
            std::string elem = parseAllocElementType();
            if (elem.empty()) return nullptr;
            std::unique_ptr<parse::Node> size;
            if (peek().kind == token::Kind::kLBracket) {
                advance();   // [
                size = parseExpr();
                if (!size) return nullptr;
                if (!expect(token::Kind::kRBracket, "]")) return nullptr;
            }
            auto node = newNodeAt(parse::Kind::kNewExpr, new_file, new_tok);
            node->return_type = widen::internOrNone(elem);
            node->children.push_back(std::move(size));   // [0] (may be null)
            node->children.push_back(std::move(addr));   // [1] (may be null)
            return node;
        }
        if (t.kind == token::Kind::kSizeof) {
            // sizeof(T) or sizeof(expr) — the byte size as an intptr. The paren
            // content is a TYPE when it starts with a type keyword (`int`,
            // `void^`, `int[]`); a bare identifier is ambiguous (alias vs
            // variable) and is parsed as an expression for resolve to dispatch
            // on (the same type-vs-value split as ##type). A string literal and
            // any other expression also take the expression path.
            int sz_file = t.file_id;
            int sz_tok = pos;
            advance();   // sizeof
            if (!expect(token::Kind::kLParen, "(")) return nullptr;
            auto node = newNodeAt(parse::Kind::kSizeofExpr, sz_file, sz_tok);
            if (isTypeStart(peek().kind)) {
                std::string ty = parseType();
                if (ty.empty()) return nullptr;
                node->return_type = widen::internOrNone(ty);
            } else {
                auto operand = parseExpr();
                if (!operand) return nullptr;
                node->children.push_back(std::move(operand));
            }
            if (!expect(token::Kind::kRParen, ")")) return nullptr;
            return node;
        }
        if (t.kind == token::Kind::kHashHash) {
            // Compile-time stringify macros. All but ##type resolve to a string
            // literal right here; ##type needs the operand's inferred type, so it
            // becomes a kStringifyType that classify lowers. The node is stamped
            // at the ## token — ##file / ##line carry the call-site location.
            int hh_file = t.file_id;
            int hh_tok = pos;
            int hh_line = t.line;
            advance();   // ##
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected a macro name after '##'.");
                return nullptr;
            }
            std::string kw = peek().text;
            int kw_tok = pos;
            advance();
            if (kw == "type") {
                if (!expect(token::Kind::kLParen, "(")) return nullptr;
                auto operand = parseExpr();
                if (!operand) return nullptr;
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
                auto node = newNodeAt(parse::Kind::kStringifyType, hh_file, hh_tok);
                node->children.push_back(std::move(operand));
                return node;
            }
            if (kw == "name") {
                // ##name reproduces its argument's lexed text verbatim — the
                // content is NOT parsed or type-checked. Scan to the matching ')'
                // by bracket depth and concatenate the token values (whitespace
                // already dropped by the lexer).
                if (!expect(token::Kind::kLParen, "(")) return nullptr;
                std::string text;
                int depth = 1;
                while (peek().kind != token::Kind::kEndOfFile
                       && peek().kind != token::Kind::kEndOfInput) {
                    token::Kind k = peek().kind;
                    if (k == token::Kind::kLParen || k == token::Kind::kLBracket
                        || k == token::Kind::kLBrace) {
                        depth++;
                    } else if (k == token::Kind::kRParen
                               || k == token::Kind::kRBracket
                               || k == token::Kind::kRBrace) {
                        if (--depth == 0) break;
                    }
                    text += peek().text;
                    advance();
                }
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
                auto node = newNodeAt(parse::Kind::kStringLiteral, hh_file, hh_tok);
                node->text = std::move(text);
                return node;
            }
            if (kw == "file" || kw == "line" || kw == "func"
                || kw == "date" || kw == "time") {
                auto node = newNodeAt(parse::Kind::kStringLiteral, hh_file, hh_tok);
                if (kw == "file") {
                    node->text = baseName(tokens.files[hh_file].path);
                } else if (kw == "line") {
                    node->text = std::to_string(hh_line);
                } else if (kw == "func") {
                    node->text = current_func;
                } else if (kw == "date") {
                    captureClock();
                    node->text = clock_date;
                } else {  // time
                    captureClock();
                    node->text = clock_time;
                }
                return node;
            }
            errorAt(kw_tok, "Unknown '##' macro '" + kw + "'.");
            return nullptr;
        }
        error("Expected expression.");
        return nullptr;
    }

    // Does this token begin a primary (operand) expression? Used to tell a
    // postfix-deref `x^` (the `^` is followed by a terminator/operator) from a
    // binary XOR `a ^ b` (the `^` is followed by an operand). Both lex to the
    // same `^`; this lookahead is the disambiguator.
    static bool startsPrimary(token::Kind k) {
        return k == token::Kind::kIdentifier
            || k == token::Kind::kColonColon
            || k == token::Kind::kIntLiteral
            || k == token::Kind::kUintLiteral
            || k == token::Kind::kCharLiteral
            || k == token::Kind::kFloatLiteral
            || k == token::Kind::kBoolLiteral
            || k == token::Kind::kStringLiteral
            || k == token::Kind::kNullptr
            || k == token::Kind::kLParen
            || k == token::Kind::kHashHash;
    }

    // Field access, indexing, postfix-^/^^, postfix-++/-- all slot in here as
    // their phases land. Today: postfix-call, postfix-deref, postfix-++/--.
    // Parse one subscript bracket onto `base`. A bracket may hold a comma-list
    // `a[x,y]` — the "natural order" form — and a comma TRANSPOSES
    // (`a[a0,..,z]` -> `a[z]...[a0]`), so the indices apply REVERSED as a chained
    // subscript: `a[x,y]` == `a[y][x]`. Positioned at `[`; consumes through `]`.
    // Shared by the expression-postfix and lvalue-store paths.
    std::unique_ptr<parse::Node> parseSubscript(std::unique_ptr<parse::Node> base) {
        int op_file = peek().file_id;
        int op_tok = pos;
        advance();   // [
        std::vector<std::unique_ptr<parse::Node>> indices;
        while (true) {
            auto index = parseExpr();
            if (!index) return nullptr;
            indices.push_back(std::move(index));
            if (peek().kind != token::Kind::kComma) break;
            advance();   // ,
        }
        if (!expect(token::Kind::kRBracket, "]")) return nullptr;
        for (std::size_t k = indices.size(); k-- > 0; ) {
            auto node = newNodeAt(parse::Kind::kIndexExpr, op_file, op_tok);
            node->children.push_back(std::move(base));
            node->children.push_back(std::move(indices[k]));
            base = std::move(node);
        }
        return base;
    }

    std::unique_ptr<parse::Node> parsePostfix(std::unique_ptr<parse::Node> base) {
        // Postfix chain: subscript `[i]` and dereference `^`, left to right.
        // A bare `^` whose following token begins an operand is binary XOR, not
        // deref — leave it for parseBitXor.
        while (true) {
            if (peek().kind == token::Kind::kLBracket) {
                base = parseSubscript(std::move(base));
                if (!base) return nullptr;
                continue;
            }
            if (peek().kind == token::Kind::kBitXor
                && !startsPrimary(peekKind(1))) {
                int op_file = peek().file_id;
                int op_tok = pos;
                advance();   // ^
                auto node = newNodeAt(parse::Kind::kDerefExpr, op_file, op_tok);
                node->children.push_back(std::move(base));
                base = std::move(node);
                continue;
            }
            break;
        }
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
        // Prefix `^` is address-of (a reference to the operand lvalue). At the
        // start of a unary, `^` is unambiguous — binary XOR never leads.
        if (k == token::Kind::kBitXor) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();   // ^
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = newNodeAt(parse::Kind::kAddrOfExpr, op_file, op_tok);
            node->children.push_back(std::move(operand));
            return node;
        }
        // Prefix `#x` describes a postfix lvalue: it desugars HERE to the 5-tuple
        // `(##file, ##line, ##type(x), ##name(x), ^x)` — all of which already
        // exist (string-literal macros, kStringifyType, address-of, tuple
        // literal). x is parsed twice (one tree for ##type, one for ^x — there is
        // no parse-node cloner) and its lexed text captured for ##name. ^x
        // enforces that x is an lvalue.
        if (k == token::Kind::kHash) {
            int h_file = peek().file_id;
            int h_tok = pos;
            int h_line = peek().line;
            advance();   // #
            int x_start = pos;
            auto x_type = parseUnary();          // tree for ##type(x)
            if (!x_type) return nullptr;
            int x_end = pos;
            std::string x_text;
            for (int i = x_start; i < x_end; i++) x_text += tokens.tokens[i].text;
            pos = x_start;
            auto x_addr = parseUnary();           // re-parse: tree for ^x
            if (!x_addr) return nullptr;

            auto strlit = [&](std::string s) {
                auto n = newNodeAt(parse::Kind::kStringLiteral, h_file, h_tok);
                n->text = std::move(s);
                return n;
            };
            auto tuple = newNodeAt(parse::Kind::kTupleExpr, h_file, h_tok);
            tuple->children.push_back(strlit(baseName(tokens.files[h_file].path)));  // ##file
            tuple->children.push_back(strlit(std::to_string(h_line)));               // ##line
            auto ty = newNodeAt(parse::Kind::kStringifyType, h_file, h_tok);         // ##type(x)
            ty->quiet_diag = true;   // the sibling ^x reports a bad operand once
            ty->children.push_back(std::move(x_type));
            tuple->children.push_back(std::move(ty));
            tuple->children.push_back(strlit(std::move(x_text)));                    // ##name(x)
            auto addr = newNodeAt(parse::Kind::kAddrOfExpr, h_file, h_tok);          // ^x
            addr->children.push_back(std::move(x_addr));
            tuple->children.push_back(std::move(addr));
            return tuple;
        }
        // Prefix `<Type^>` is a pointer reinterpret cast. A `<` leading a unary
        // operand is unambiguous — binary `<` (less-than) only appears in a
        // comparison, where its right operand never starts with `<`. The cast
        // binds like a prefix unary (precedence 3), so the operand is another
        // unary — chained casts `<A^> <void^> x` nest right-to-left.
        if (k == token::Kind::kLt) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();   // <
            std::vector<int> target_seg_toks;
            std::string target = parseType(&target_seg_toks);
            if (target.empty()) return nullptr;
            if (!expect(token::Kind::kGt, ">")) return nullptr;
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = newNodeAt(parse::Kind::kCastExpr, op_file, op_tok);
            node->return_type = widen::internOrNone(target);
            node->return_type_seg_toks = std::move(target_seg_toks);
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

    std::unique_ptr<parse::Node> makeIdent(std::string name, int file, int tok) {
        auto node = newNodeAt(parse::Kind::kIdentExpr, file, tok);
        node->name = std::move(name);
        node->name_tok = tok;
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
        // Typeless const — `const name = expr;` infers its type from the rhs. The
        // `=` right after the name disambiguates from `const Type name = ...`.
        bool typeless = is_const
            && peek().kind == token::Kind::kIdentifier
            && peekKind(1) == token::Kind::kEquals;
        std::string type;
        std::vector<int> type_seg_toks;
        if (!typeless) {
            type = parseType(&type_seg_toks);
            if (fatal) return nullptr;
        }
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
        // Fixed-size array dimensions follow the NAME: `int arr[5]`,
        // `int grid[3][5]` (standard row-major: the leftmost dim is the
        // OUTERMOST). A single bracket may hold a comma-list `int grid[3,5]`,
        // the "natural order" form; a comma TRANSPOSES (`[a,..,z]` -> `[z]...[a]`),
        // so each bracket's dims are appended REVERSED. A dim that is a
        // const-EXPRESSION rather than a literal (`arr[N]`, `arr[sizeof(int)]`)
        // parses as an expression, bakes a provisional `[1]` into the spelling,
        // and is folded + baked for real in constfold.
        std::vector<std::unique_ptr<parse::Node>> dim_exprs;
        bool any_dim_expr = false;
        while (peek().kind == token::Kind::kLBracket) {
            advance();   // [
            std::vector<std::string> bracket_spell;
            std::vector<std::unique_ptr<parse::Node>> bracket_expr;
            while (true) {
                if (peek().kind == token::Kind::kIntLiteral) {
                    // A LITERAL dim is a known constant — validate its positivity
                    // here (a const-EXPRESSION dim is validated, after folding, in
                    // constfold's bakeNodeDims). It bakes straight into the
                    // spelling, so it carries no dim_expr.
                    if (std::strtoll(peek().text.c_str(), nullptr, 10) <= 0) {
                        error("Array size must be a positive integer constant.");
                        return nullptr;
                    }
                    bracket_spell.push_back(peek().text);
                    advance();   // size
                    bracket_expr.push_back(nullptr);
                } else {
                    auto dim = parseExpr();
                    if (!dim) return nullptr;
                    bracket_spell.push_back("1");   // provisional; constfold bakes it
                    bracket_expr.push_back(std::move(dim));
                    any_dim_expr = true;
                }
                if (peek().kind != token::Kind::kComma) break;
                advance();   // ,
            }
            if (!expect(token::Kind::kRBracket, "]")) return nullptr;
            // Append this bracket's dims reversed (the comma transpose).
            for (std::size_t k = bracket_spell.size(); k-- > 0; ) {
                type += "[" + bracket_spell[k] + "]";
                dim_exprs.push_back(std::move(bracket_expr[k]));
            }
        }
        if (any_dim_expr) node->dim_exprs = std::move(dim_exprs);
        node->return_type = widen::internOrNone(type);
        node->return_type_seg_toks = std::move(type_seg_toks);
        node->is_const = is_const;
        if (peek().kind == token::Kind::kEquals
            || peek().kind == token::Kind::kArrowLeft) {
            // `<--` is a move-init: the same copy as `=`, then desugar nulls the
            // init's pointer leaves. (`<-->` swap needs two existing values, so
            // it is not a declaration form.)
            node->move_init = (peek().kind == token::Kind::kArrowLeft);
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

    // Build a move (`<--`) or swap (`<-->`) statement from an already-parsed lhs
    // lvalue expression. Positioned at the arrow token; consumes the arrow, the
    // rhs expression, and the trailing `;`. children[0] = lhs, [1] = rhs.
    std::unique_ptr<parse::Node> finishMoveSwap(std::unique_ptr<parse::Node> lhs,
                                                int stmt_file, int stmt_tok) {
        parse::Kind k = (peek().kind == token::Kind::kArrowBoth)
            ? parse::Kind::kSwapStmt : parse::Kind::kMoveStmt;
        advance();   // <-- or <-->
        auto rhs = parseExpr();
        if (!rhs) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = newNodeAt(k, stmt_file, stmt_tok);
        node->children.push_back(std::move(lhs));
        node->children.push_back(std::move(rhs));
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
        if (next == token::Kind::kBitXor || next == token::Kind::kLBracket) {
            // Lvalue-expression store: `name[i]... = rhs` or `name^ = rhs`. The
            // bare name becomes a kIdentExpr, then the postfix chain (subscripts
            // and derefs, left to right) wraps it into the store target. (A `^`
            // here is unambiguously deref — a trailing operand would be XOR, not
            // a statement.)
            auto lhs = newNodeAt(parse::Kind::kIdentExpr, stmt_file, stmt_tok);
            lhs->name = name;
            lhs->name_tok = name_tok;
            lhs->qualifier = segs;
            lhs->qualifier_toks = toks;
            lhs->global_qualified = global;
            while (peek().kind == token::Kind::kLBracket
                   || peek().kind == token::Kind::kBitXor) {
                if (peek().kind == token::Kind::kLBracket) {
                    lhs = parseSubscript(std::move(lhs));   // comma-aware (transposes)
                    if (!lhs) return nullptr;
                } else {
                    int op_file = peek().file_id;
                    int op_tok = pos;
                    advance();   // ^
                    auto d = newNodeAt(parse::Kind::kDerefExpr, op_file, op_tok);
                    d->children.push_back(std::move(lhs));
                    lhs = std::move(d);
                }
            }
            if (peek().kind == token::Kind::kArrowLeft
                || peek().kind == token::Kind::kArrowBoth) {
                return finishMoveSwap(std::move(lhs), stmt_file, stmt_tok);
            }
            if (!expect(token::Kind::kEquals, "=")) return nullptr;
            auto rhs = parseExpr();
            if (!rhs) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kStoreStmt, stmt_file, stmt_tok);
            node->children.push_back(std::move(lhs));
            node->children.push_back(std::move(rhs));
            return node;
        }
        if (next == token::Kind::kLParen) {
            advance();   // (
            auto node = newNodeAt(parse::Kind::kCallStmt, stmt_file, stmt_tok);
            stamp(*node);
            if (!parseCallArgs(*node)) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            return node;
        }
        if (next == token::Kind::kArrowLeft || next == token::Kind::kArrowBoth) {
            // `name <-- rhs;` / `name <--> rhs;` — move / swap with a bare-name
            // lhs. The lhs is an lvalue EXPRESSION (a kIdentExpr), uniform with
            // the indexed/deref store path, so desugar treats every lhs alike.
            auto lhs = newNodeAt(parse::Kind::kIdentExpr, stmt_file, stmt_tok);
            lhs->name = name;
            lhs->name_tok = name_tok;
            lhs->qualifier = segs;
            lhs->qualifier_toks = toks;
            lhs->global_qualified = global;
            return finishMoveSwap(std::move(lhs), stmt_file, stmt_tok);
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

    std::unique_ptr<parse::Node> parseDeleteStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // delete
        auto node = newNodeAt(parse::Kind::kDeleteStmt, stmt_file, stmt_tok);
        auto operand = parseExpr();   // the pointer lvalue; resolve checks it
        if (!operand) return nullptr;
        node->children.push_back(std::move(operand));
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // Parse the element type of a `new`: a primitive or (possibly qualified)
    // identifier type name. Unlike parseType it does NOT consume a trailing `[`
    // — that introduces the array-size expression (`new T[n]`), not an iterator
    // suffix. Returns the spelling, or "" on error.
    std::string parseAllocElementType() {
        std::string type;
        if (char const* name = primitiveNameFor(peek().kind)) {
            type = name;
            advance();
        } else if (peek().kind == token::Kind::kIdentifier
                   || peek().kind == token::Kind::kColonColon) {
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
            error("Expected a type after 'new'.");
            return "";
        }
        return type;
    }

    std::unique_ptr<parse::Node> parseReturnStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // return
        auto node = newNodeAt(parse::Kind::kReturnStmt, stmt_file, stmt_tok);
        // Bare `return;` (no value) — valid in a void function; classify rejects
        // it in a non-void one. Otherwise a returned expression.
        if (peek().kind != token::Kind::kSemicolon) {
            auto expr = parseExpr();
            if (!expr) return nullptr;
            node->children.push_back(std::move(expr));
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
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
            node->return_type = widen::internOrNone(target);
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
        node->return_type = widen::internOrNone(underlying);
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
            m->return_type = widen::internOrNone(underlying);
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
        auto cond = parseParenCondition();   // empty `()` -> always-true literal
        if (!cond) return nullptr;
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
    // A loop's optional `:label` after its body (labels go on loops only this
    // round — never a switch). Returns the label name, or "" if none. The
    // label name is a plain identifier (the for/while keyword DEFAULT names are
    // only for referencing in break/continue, not for declaring).
    std::string parseOptionalLabel() {
        if (peek().kind != token::Kind::kColon) return "";
        advance();   // :
        if (peek().kind != token::Kind::kIdentifier) {
            error("Expected a label name after ':'.");
            return "";
        }
        std::string name = peek().text;
        advance();   // name
        return name;
    }

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
            std::string label = parseOptionalLabel();   // between `}` and `(cond)`
            if (fatal) return nullptr;
            auto cond = parseParenCondition();
            if (!cond) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kDoWhileStmt, stmt_file, stmt_tok);
            node->children.push_back(std::move(cond));
            node->children.push_back(std::move(body));
            node->label = std::move(label);
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
        node->label = parseOptionalLabel();              // after the body `}`
        if (fatal) return nullptr;
        if (!node->label.empty() && !expect(token::Kind::kSemicolon, ";")) {
            return nullptr;
        }
        return node;
    }

    // Build a long-for's `( cond ) { update } { body }` tail and assemble the
    // node from an already-parsed varlist. children[0]=cond, [1]=update-block,
    // [2]=body-block, [3..]=varlist decls.
    std::unique_ptr<parse::Node> finishLongFor(int stmt_file, int stmt_tok,
            std::vector<std::unique_ptr<parse::Node>> varlist) {
        auto cond = parseParenCondition();   // `( cond )`, empty -> true
        if (!cond) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' for the for-loop update clause.");
            return nullptr;
        }
        auto update = parseBlock();
        if (!update) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' for the for-loop body.");
            return nullptr;
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        auto node = newNodeAt(parse::Kind::kForLongStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(update));
        node->children.push_back(std::move(body));
        for (auto& d : varlist) node->children.push_back(std::move(d));
        node->label = parseOptionalLabel();              // after the body `}`
        if (fatal) return nullptr;
        if (!node->label.empty() && !expect(token::Kind::kSemicolon, ";")) {
            return nullptr;
        }
        return node;
    }

    // Ranged-for: `for (type var : start .. [cmp] end [op step]) {body}`.
    // Builds a kForRangedStmt holding the raw clauses; resolve/constfold/classify
    // understand it in scope and desugar lowers it to the canonical kForLongStmt
    // (synthesizing the `_$end`/`_$step` bound/step locals there, with fresh ids).
    // The `..` token rides along for the empty-range "Invalid range." check.
    std::unique_ptr<parse::Node> parseRangeFor(int stmt_file, int stmt_tok,
            std::string vtype, std::string vname, int v_file, int v_tok,
            int vname_tok, std::unique_ptr<parse::Node> start) {
        // `start` is the operand before `..`, already parsed by the caller.
        int dotdot_tok = pos;
        advance();   // ..
        std::string cmp = "<";
        token::Kind ck = peek().kind;
        if      (ck == token::Kind::kLt)    { cmp = "<";  advance(); }
        else if (ck == token::Kind::kLtEq)  { cmp = "<="; advance(); }
        else if (ck == token::Kind::kGt)    { cmp = ">";  advance(); }
        else if (ck == token::Kind::kGtEq)  { cmp = ">="; advance(); }
        else if (ck == token::Kind::kNotEq) { cmp = "!="; advance(); }
        else if (ck == token::Kind::kEqEq)  {
            // `==` is not a range comparator (a range is half-open / ordered).
            error("Invalid range comparator; use '<', '<=', '>', '>=', or '!='.");
            return nullptr;
        }
        auto end = parseUnary();
        if (!end) return nullptr;
        std::string op;
        token::Kind ok = peek().kind;
        if      (ok == token::Kind::kPlus)   op = "+";
        else if (ok == token::Kind::kMinus)  op = "-";
        else if (ok == token::Kind::kStar)   op = "*";
        else if (ok == token::Kind::kSlash)  op = "/";
        else if (ok == token::Kind::kLShift) op = "<<";
        else if (ok == token::Kind::kRShift) op = ">>";
        // A recognized-but-unsupported operator in the step position (`%`, `&`,
        // `|`, `^`, `&&`, `||`, `^^`) is rejected clearly rather than left to a
        // misleading "Expected ')'". (A modulus etc. INSIDE a parenthesized bound
        // is fine — parseUnary consumed it, so this only sees a BARE operator.)
        else if (ok == token::Kind::kPercent || ok == token::Kind::kBitAnd
              || ok == token::Kind::kBitOr   || ok == token::Kind::kBitXor
              || ok == token::Kind::kAnd     || ok == token::Kind::kOr
              || ok == token::Kind::kXorXor) {
            error("Invalid range step operator; use '+', '-', '*', '/', "
                  "'<<', or '>>'.");
            return nullptr;
        }
        std::unique_ptr<parse::Node> step;
        if (!op.empty()) {
            advance();   // op
            step = parseUnary();
            if (!step) return nullptr;
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' for the for-loop body.");
            return nullptr;
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        std::string label = parseOptionalLabel();        // after the body `}`
        if (fatal) return nullptr;
        if (!label.empty() && !expect(token::Kind::kSemicolon, ";")) {
            return nullptr;
        }

        // Short form: the loop-var decl (init = start), then the raw end / step
        // clauses. The `_$end`/`_$step` bound and step locals are NOT synthesized
        // here — desugar mints them with fresh ids while lowering to kForLongStmt,
        // so the old name-reuse `_$end` clobber can't arise. A missing step op
        // defaults to `+` (a +1 step); a null step child carries that default.
        auto vd = newNodeAt(parse::Kind::kVarDeclStmt, v_file, v_tok);
        vd->name = vname; vd->name_tok = vname_tok;
        vd->return_type = widen::internOrNone(vtype);
        vd->children.push_back(std::move(start));

        auto node = newNodeAt(parse::Kind::kForRangedStmt, stmt_file, stmt_tok);
        node->text = cmp;                          // comparison operator
        node->name = op.empty() ? "+" : op;        // step operator (default +)
        node->children.push_back(std::move(vd));   // [0] loop-var decl (init=start)
        node->children.push_back(std::move(end));  // [1] end expr
        node->children.push_back(std::move(step)); // [2] step expr (null => +1)
        node->children.push_back(std::move(body)); // [3] body block
        node->range_dotdot_tok = dotdot_tok;
        node->label = std::move(label);
        return node;
    }

    // for-enum: `for (var : Enum) {body}`. `enum_ref` is the operand after ':'
    // (must be a bare/qualified identifier — the enum name). Builds a kForEnumStmt
    // {loop-var decl, enum-ref, body}; resolve lowers it once the enum is known.
    std::unique_ptr<parse::Node> parseEnumFor(int stmt_file, int stmt_tok,
            std::string vtype, std::string vname, int v_file, int v_tok,
            int vname_tok, std::unique_ptr<parse::Node> enum_ref) {
        // The operand after ':' is any expression (parseUnary already parsed it):
        // an enum NAME, an array/tuple VARIABLE, a tuple LITERAL, or any other
        // expression resolving to a homogeneous tuple (a deref `ref^`, an index,
        // a function call, ...). resolve dispatches on its resolved TYPE and
        // rejects a non-iterable there.
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' for the for-loop body.");
            return nullptr;
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        std::string label = parseOptionalLabel();        // after the body `}`
        if (fatal) return nullptr;
        if (!label.empty() && !expect(token::Kind::kSemicolon, ";")) {
            return nullptr;
        }
        auto vd = newNodeAt(parse::Kind::kVarDeclStmt, v_file, v_tok);
        vd->name = vname; vd->name_tok = vname_tok; vd->return_type = widen::internOrNone(vtype);
        auto node = newNodeAt(parse::Kind::kForEnumStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(vd));        // [0] loop-var decl
        node->children.push_back(std::move(enum_ref));  // [1] enum-ref
        node->children.push_back(std::move(body));      // [2] body
        node->label = std::move(label);
        return node;
    }

    // for — dispatches between the colon (ranged/enum) form and the long form on
    // the token after the first `[type] var`: ':' opens a range or enum,
    // anything else makes that decl varlist[0] of a long-for. An empty `()` is an
    // empty-varlist long form. A var type is optional (parseForVarHead); a
    // typeless var is inferred from its initializer or reuses an enclosing local.
    std::unique_ptr<parse::Node> parseForStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // for
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        if (peek().kind == token::Kind::kRParen) {
            advance();   // )  — empty varlist long-for
            return finishLongFor(stmt_file, stmt_tok, {});
        }
        // First decl's `[type] name` (type optional — typeless infers / reuses).
        int v_file, v_tok, vname_tok;
        std::string vtype, vname;
        if (!parseForVarHead(vtype, vname, v_file, v_tok, vname_tok)) {
            return nullptr;
        }
        if (peek().kind == token::Kind::kColon) {
            advance();   // :
            auto operand = parseUnary();   // start (range) or the enum name
            if (!operand) return nullptr;
            if (peek().kind == token::Kind::kDotDot) {
                return parseRangeFor(stmt_file, stmt_tok, std::move(vtype),
                                     std::move(vname), v_file, v_tok, vname_tok,
                                     std::move(operand));
            }
            return parseEnumFor(stmt_file, stmt_tok, std::move(vtype),
                                std::move(vname), v_file, v_tok, vname_tok,
                                std::move(operand));
        }
        // Long form: varlist[0] is the decl just parsed; gather any more.
        std::vector<std::unique_ptr<parse::Node>> varlist;
        while (true) {
            auto decl = newNodeAt(parse::Kind::kVarDeclStmt, v_file, v_tok);
            decl->name = vname;
            decl->name_tok = vname_tok;
            decl->return_type = widen::internOrNone(vtype);
            if (peek().kind == token::Kind::kEquals) {
                advance();   // =
                auto init = parseExpr();
                if (!init) return nullptr;
                decl->children.push_back(std::move(init));
            }
            varlist.push_back(std::move(decl));
            if (peek().kind != token::Kind::kComma) break;
            advance();   // ,
            if (!parseForVarHead(vtype, vname, v_file, v_tok, vname_tok)) {
                return nullptr;
            }
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        return finishLongFor(stmt_file, stmt_tok, std::move(varlist));
    }

    // break / continue, with an optional argument: an integer (the Nth enclosing
    // loop, stored in `text`) or a name (a loop label, stored in `name` — incl.
    // the `for` / `while` keyword default names). No argument = naked.
    std::unique_ptr<parse::Node> parseBreakContinue(parse::Kind kind) {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // break / continue
        auto node = newNodeAt(kind, stmt_file, stmt_tok);
        token::Kind k = peek().kind;
        if (k == token::Kind::kIntLiteral) {
            node->text = peek().text;        // numbered
            node->name_tok = pos;            // the count token (for the caret)
            advance();
        } else if (k == token::Kind::kIdentifier
                   || k == token::Kind::kFor || k == token::Kind::kWhile) {
            node->name = peek().text;        // named (label or for/while default)
            node->name_tok = pos;
            advance();
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // One `case const-expr:` / `default:` clause. children[0] = label const-expr
    // (nullptr for default), [1] = body kBlockStmt (statements up to the next
    // case/default/`}`). The body is its own lexical scope; fall-through to the
    // next clause is a resolve/codegen concern.
    std::unique_ptr<parse::Node> parseCaseClause() {
        int clause_file = peek().file_id;
        int clause_tok = pos;
        std::unique_ptr<parse::Node> label;
        if (peek().kind == token::Kind::kCase) {
            advance();   // case
            case_label_ = true;
            label = parseExpr();
            case_label_ = false;
            if (!label) return nullptr;
        } else if (peek().kind == token::Kind::kDefault) {
            advance();   // default — label stays null
        } else {
            error("Expected 'case' or 'default' in the switch body.");
            return nullptr;
        }
        if (!expect(token::Kind::kColon, ":")) return nullptr;
        auto body = newNodeAt(parse::Kind::kBlockStmt, clause_file, clause_tok);
        while (peek().kind != token::Kind::kCase
               && peek().kind != token::Kind::kDefault
               && peek().kind != token::Kind::kRBrace
               && peek().kind != token::Kind::kEndOfFile
               && peek().kind != token::Kind::kEndOfInput) {
            auto stmt = parseStmt();
            if (!stmt) return nullptr;
            body->children.push_back(std::move(stmt));
        }
        auto clause = newNodeAt(parse::Kind::kCaseClause, clause_file, clause_tok);
        clause->children.push_back(std::move(label));   // [0] (null => default)
        clause->children.push_back(std::move(body));     // [1]
        return clause;
    }

    // switch ( value ) { clause* } — value required; clauses are case/default.
    std::unique_ptr<parse::Node> parseSwitchStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // switch
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        if (peek().kind == token::Kind::kRParen) {
            error("A switch value is required.");
            return nullptr;
        }
        auto scrutinee = parseExpr();
        if (!scrutinee) return nullptr;
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;
        auto node = newNodeAt(parse::Kind::kSwitchStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(scrutinee));   // [0]
        while (peek().kind != token::Kind::kRBrace
               && peek().kind != token::Kind::kEndOfFile
               && peek().kind != token::Kind::kEndOfInput) {
            auto clause = parseCaseClause();
            if (!clause) return nullptr;
            node->children.push_back(std::move(clause));
        }
        if (!expect(token::Kind::kRBrace, "}")) return nullptr;
        return node;
    }

    std::unique_ptr<parse::Node> parseStmt() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kLBrace) return parseBlock();
        if (t.kind == token::Kind::kIf) return parseIfStmt();
        if (t.kind == token::Kind::kWhile) return parseWhileStmt();
        if (t.kind == token::Kind::kFor) return parseForStmt();
        if (t.kind == token::Kind::kSwitch) return parseSwitchStmt();
        if (t.kind == token::Kind::kBreak)
            return parseBreakContinue(parse::Kind::kBreakStmt);
        if (t.kind == token::Kind::kContinue)
            return parseBreakContinue(parse::Kind::kContinueStmt);
        if (t.kind == token::Kind::kReturn) return parseReturnStmt();
        if (t.kind == token::Kind::kDelete) return parseDeleteStmt();
        if (t.kind == token::Kind::kConst) return parseVarDeclStmt();
        if (t.kind == token::Kind::kAlias) return parseAliasDecl();
        if (t.kind == token::Kind::kEnum) return parseEnumDecl();
        // A nested function definition (`type name (params) {body}`) — checked
        // before the var-decl / name-led dispatches it would otherwise hit.
        if (looksLikeFunctionDef()) return parseFunctionDef();
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
            // A qualified (`Space:Dir x`) or pointer-suffixed (`Integer^ ref`,
            // `Integer[] iter`) identifier-typed decl. looksLikeQualifiedTypedDecl
            // accepts an optional `^` / `[]` suffix before the var name.
            if ((next == token::Kind::kColon
                 || next == token::Kind::kBitXor
                 || next == token::Kind::kLBracket)
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
        // A leading `(...)` is a tuple-typed var decl when a name follows the
        // `)` (`(Dir, bool) pair = ...`), or a destructure when `=` follows
        // (`(a, b) = tuple`); otherwise it's an expression statement.
        if (t.kind == token::Kind::kLParen) {
            if (looksLikeTupleTypeDecl()) return parseVarDeclStmt();
            if (looksLikeTupleDestructure()) return parseDestructureStmt();
        }
        error("Expected statement.");
        return nullptr;
    }

    // `(a, b, ) = tuple;` — a tuple destructure. children[0] = rhs tuple expr;
    // [1..] = target lvalues in slot order, a NULL child for an empty/skipped
    // slot. Targets are parsed first (syntactic order) into [1..], then the rhs
    // is filled into [0].
    std::unique_ptr<parse::Node> parseDestructureStmt() {
        int lp = pos;
        auto node = newNodeAt(parse::Kind::kDestructureStmt, peek().file_id, lp);
        node->children.push_back(nullptr);   // [0] = rhs, filled below
        advance();   // (
        while (true) {
            if (peek().kind == token::Kind::kComma
                || peek().kind == token::Kind::kRParen) {
                node->children.push_back(nullptr);   // empty / skipped slot
            } else {
                auto tgt = parseExpr();
                if (!tgt) return nullptr;
                node->children.push_back(std::move(tgt));
            }
            if (peek().kind != token::Kind::kComma) break;
            advance();   // ,
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (!expect(token::Kind::kEquals, "=")) return nullptr;
        auto rhs = parseExpr();
        if (!rhs) return nullptr;
        node->children[0] = std::move(rhs);
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // Pure lookahead: do the tokens form `<return-type> <name> (` — a (nested)
    // function definition — as opposed to a var decl (`type name = / ;`) or a
    // call (`name(...)`, no leading type)? Consumes nothing.
    bool looksLikeFunctionDef() const {
        int o = 0;
        if (isTypeStart(peekKind(o))) {
            o++;
        } else if (peekKind(o) == token::Kind::kColonColon
                   || peekKind(o) == token::Kind::kIdentifier) {
            if (peekKind(o) == token::Kind::kColonColon) o++;
            if (peekKind(o) != token::Kind::kIdentifier) return false;
            o++;
            while (peekKind(o) == token::Kind::kColon
                   && peekKind(o + 1) == token::Kind::kIdentifier) o += 2;
        } else {
            return false;
        }
        if (peekKind(o) == token::Kind::kLBracket
            && peekKind(o + 1) == token::Kind::kRBracket) o += 2;
        if (peekKind(o) != token::Kind::kIdentifier) return false;   // fn name
        o++;
        return peekKind(o) == token::Kind::kLParen;
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
        node->return_type = widen::internOrNone(ret_type);

        while (peek().kind != token::Kind::kRParen) {
            int p_file = peek().file_id;
            int p_tok = pos;
            // `[type] name [= constexpr]` — the type is optional; a typeless
            // param infers its type from its default value (resolve/classify
            // enforce that a typeless param HAS a default, and required-before-
            // optional ordering).
            std::string p_type;
            if (isTypeStart(peek().kind) || looksLikeQualifiedTypedDecl()
                || (peek().kind == token::Kind::kLParen
                    && looksLikeTupleTypeDecl())) {
                p_type = parseType();
                if (fatal) return nullptr;
            }
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
            p->return_type = widen::internOrNone(p_type);
            if (peek().kind == token::Kind::kEquals) {
                advance();   // =
                auto def = parseExpr();
                if (!def) return nullptr;
                p->children.push_back(std::move(def));   // children[0] = default
            }
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

        std::string saved_func = current_func;
        current_func = node->name;   // ##func in the body names this function
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
        current_func = std::move(saved_func);
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
