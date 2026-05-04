#include "parser.h"
#include "lexer.h"
#include "source_map.h"
#include <stdexcept>
#include <functional>
#include <fstream>
#include <sstream>
#include <map>

Parser::Parser(SourceMap& sm, int file_id, std::vector<Token> tokens,
               std::string source_dir,
               std::vector<std::string> import_paths,
               std::shared_ptr<std::set<std::string>> imported_once)
    : sm_(sm), file_id_(file_id),
      tokens_(std::move(tokens)), pos_(0), source_dir_(std::move(source_dir)),
      import_paths_(std::move(import_paths)),
      imported_once_(imported_once ? std::move(imported_once)
                                   : std::make_shared<std::set<std::string>>()) {}

void Parser::errorHere(const std::string& msg) {
    [[maybe_unused]] int t_start = pos_;
    throw CompileError{file_id_, pos_, msg};
}

void Parser::errorAt(int t, const std::string& msg) {
    [[maybe_unused]] int t_start = pos_;
    throw CompileError{file_id_, t, msg};
}

int Parser::currentLine() {
    [[maybe_unused]] int t_start = pos_;
    auto& tlocs = sm_.at(file_id_).tokens;
    if (pos_ < 0 || pos_ >= (int)tlocs.size()) return 0;
    return tlocs[pos_].line;
}

void Parser::declareVar(const std::string& name, int name_tok) {
    // (P2) inside any method, no binding may shadow a field of the enclosing class.
    if (current_slid_fields_.count(name)) {
        errorAt(name_tok, "'" + name + "' shadows field of enclosing class");
    }
    if (!scope_stack_.empty()) {
        if (!scope_stack_.back().emplace(name, LocalInfo{}).second) {
            errorAt(name_tok, "local '" + name + "' already declared in same scope");
        }
    }
}

static void rejectReserved(int file_id, int tok, const std::string& name, const char* role) {
    // 'self' is now a kSelf keyword token at the lexer level — never reaches here
    // as an identifier. Function/class/method/enum names that try to use 'self'
    // hit a token-mismatch error at the parser level instead.
    (void)file_id; (void)tok; (void)name; (void)role;
}

bool Parser::isInScope(const std::string& name) const {
    [[maybe_unused]] int t_start = pos_;
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it)
        if (it->count(name)) return true;
    return false;
}

Parser::LocalInfo* Parser::findLocal(const std::string& name) {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto sit = it->find(name);
        if (sit != it->end()) return &sit->second;
    }
    return nullptr;
}

int Parser::arrayCountInScope(const std::string& name) const {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto sit = it->find(name);
        if (sit != it->end()) return sit->second.is_array ? sit->second.array_count : 0;
    }
    return 0;
}

int Parser::arrayRankInScope(const std::string& name) const {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto sit = it->find(name);
        if (sit != it->end()) return sit->second.is_array ? sit->second.array_rank : 0;
    }
    return 0;
}

int Parser::tupleSizeInScope(const std::string& name) const {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto sit = it->find(name);
        if (sit != it->end()) return sit->second.is_tuple ? sit->second.tuple_count : 0;
    }
    return 0;
}

std::string Parser::typeInScope(const std::string& name) const {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto sit = it->find(name);
        if (sit != it->end()) return sit->second.type;
    }
    return "";
}

void Parser::recordSlidMethods(const SlidDef& s) {
    auto& info = class_info_[s.name];
    for (auto& m : s.methods) {
        info.method_names.insert(m.name);
        MethodSig sig;
        sig.return_type = m.return_type;
        for (auto& p : m.params) sig.param_types.push_back(p.first);
        info.sigs[m.name] = std::move(sig);
    }
}

Parser::ProtocolDiag Parser::classifyByValue(const ClassInfo& ci,
                                              const std::string& cname) const {
    bool has_op = ci.method_names.count("op[]") != 0;
    bool has_size = ci.method_names.count("size") != 0;
    ProtocolDiag d;
    if (!has_op && !has_size) { d.status = ProtocolStatus::Absent; return d; }
    if (!has_op || !has_size) {
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' defines "
            + std::string(has_op ? "op[] but not size" : "size but not op[]");
        return d;
    }
    auto& op_sig = ci.sigs.at("op[]");
    auto& sz_sig = ci.sigs.at("size");
    if (op_sig.param_types.size() != 1) {
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' op[] must take 1 parameter, got "
            + std::to_string(op_sig.param_types.size());
        return d;
    }
    if (!sz_sig.param_types.empty()) {
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' size must take 0 parameters, got "
            + std::to_string(sz_sig.param_types.size());
        return d;
    }
    d.status = ProtocolStatus::Good;
    d.return_type = op_sig.return_type;
    return d;
}

Parser::ProtocolDiag Parser::classifyByRef(const ClassInfo& ci,
                                            const std::string& cname) const {
    bool has_b = ci.method_names.count("begin") != 0;
    bool has_e = ci.method_names.count("end") != 0;
    bool has_n = ci.method_names.count("next") != 0;
    ProtocolDiag d;
    int present = (int)has_b + (int)has_e + (int)has_n;
    if (present == 0) { d.status = ProtocolStatus::Absent; return d; }
    if (present < 3) {
        std::string missing;
        auto add = [&](const char* nm) {
            if (!missing.empty()) missing += ", ";
            missing += nm;
        };
        if (!has_b) add("begin");
        if (!has_e) add("end");
        if (!has_n) add("next");
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' defines some of begin/end/next but not all (missing: "
            + missing + ")";
        return d;
    }
    auto& b_sig = ci.sigs.at("begin");
    auto& e_sig = ci.sigs.at("end");
    auto& n_sig = ci.sigs.at("next");
    if (b_sig.return_type != e_sig.return_type
            || b_sig.return_type != n_sig.return_type) {
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' begin/end/next return types differ: begin returns '"
            + b_sig.return_type + "', end returns '" + e_sig.return_type
            + "', next returns '" + n_sig.return_type + "'";
        return d;
    }
    if (!b_sig.param_types.empty()) {
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' begin must take 0 parameters, got "
            + std::to_string(b_sig.param_types.size());
        return d;
    }
    if (!e_sig.param_types.empty()) {
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' end must take 0 parameters, got "
            + std::to_string(e_sig.param_types.size());
        return d;
    }
    if (n_sig.param_types.size() != 1) {
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' next must take 1 parameter, got "
            + std::to_string(n_sig.param_types.size());
        return d;
    }
    if (n_sig.param_types[0] != b_sig.return_type) {
        d.status = ProtocolStatus::Bad;
        d.reason = "type '" + cname + "' next parameter type '" + n_sig.param_types[0]
            + "' must match begin/end/next return type '" + b_sig.return_type + "'";
        return d;
    }
    d.status = ProtocolStatus::Good;
    d.return_type = b_sig.return_type;
    return d;
}

Token& Parser::peek() { return tokens_[pos_]; }

static bool isKeyword(TokenType t) {
    switch (t) {
        case TokenType::kInt: case TokenType::kInt8: case TokenType::kInt16:
        case TokenType::kInt32: case TokenType::kInt64: case TokenType::kIntptr:
        case TokenType::kUint: case TokenType::kUint8: case TokenType::kUint16:
        case TokenType::kUint32: case TokenType::kUint64: case TokenType::kChar:
        case TokenType::kFloat32: case TokenType::kFloat64:
        case TokenType::kBool: case TokenType::kVoid:
        case TokenType::kReturn: case TokenType::kTrue: case TokenType::kFalse:
        case TokenType::kIf: case TokenType::kElse:
        case TokenType::kWhile: case TokenType::kFor:
        case TokenType::kBreak: case TokenType::kContinue:
        case TokenType::kEnum: case TokenType::kSwitch:
        case TokenType::kCase: case TokenType::kDefault:
        case TokenType::kNew: case TokenType::kDelete: case TokenType::kNullptr:
        case TokenType::kImport: case TokenType::kVirtual:
        case TokenType::kSizeof: case TokenType::kOp: case TokenType::kMutable:
            return true;
        default:
            return false;
    }
}

static std::string tokenLabel(const Token& t) {
    return isKeyword(t.type)
        ? "keyword '" + t.value + "'"
        : "'" + t.value + "'";
}

Token& Parser::advance() {
    [[maybe_unused]] int t_start = pos_;
    Token& t = tokens_[pos_];
    if (t.type != TokenType::kEof) pos_++;
    return t;
}

Token& Parser::expect(TokenType type, const std::string& msg) {
    [[maybe_unused]] int t_start = pos_;
    if (peek().type != type)
        errorHere(msg + ", got " + tokenLabel(peek()));
    return advance();
}

bool Parser::isTypeName(const Token& t) const {
    [[maybe_unused]] int t_start = pos_;
    switch (t.type) {
        case TokenType::kInt: case TokenType::kInt8: case TokenType::kInt16:
        case TokenType::kInt32: case TokenType::kInt64:
        case TokenType::kUint: case TokenType::kUint8: case TokenType::kUint16:
        case TokenType::kUint32: case TokenType::kUint64: case TokenType::kChar:
        case TokenType::kIntptr:
        case TokenType::kFloat32: case TokenType::kFloat64:
        case TokenType::kBool: case TokenType::kVoid:
            return true;
        default: return false;
    }
}

bool Parser::isUserTypeName(const Token& t) const {
    [[maybe_unused]] int t_start = pos_;
    return t.type == TokenType::kIdentifier;
}

bool Parser::isVarDeclLookahead() const {
    [[maybe_unused]] int t_start = pos_;
    // pos_ is at an identifier that might be a type name.
    // scan past it (plus optional :Qualifier, template args and pointer/iterator suffix)
    // and check that an identifier (the variable name) follows.
    int i = pos_ + 1; // skip base type name
    // optional qualified-type suffix: : Identifier (e.g. Outer:Inner)
    if (i + 1 < (int)tokens_.size()
        && tokens_[i].type == TokenType::kColon
        && tokens_[i + 1].type == TokenType::kIdentifier) {
        i += 2;
    }
    // optional template args: <Types>
    if (i < (int)tokens_.size() && tokens_[i].type == TokenType::kLt) {
        i++;
        while (i < (int)tokens_.size() && tokens_[i].type != TokenType::kGt
               && tokens_[i].type != TokenType::kEof) i++;
        if (i >= (int)tokens_.size() || tokens_[i].type != TokenType::kGt) return false;
        i++;
    }
    // optional ^ or []
    if (i < (int)tokens_.size() && tokens_[i].type == TokenType::kBitXor) i++;
    else if (i + 1 < (int)tokens_.size()
             && tokens_[i].type == TokenType::kLBracket
             && tokens_[i+1].type == TokenType::kRBracket) i += 2;
    return i < (int)tokens_.size() && tokens_[i].type == TokenType::kIdentifier;
}

// Pointer-shaped type: ends with '^' (ref) or '[]' (iterator).
// 'mutable' applies only to these.
static bool isParamIndirectType(const std::string& t) {
    if (!t.empty() && t.back() == '^') return true;
    if (t.size() >= 2 && t.substr(t.size()-2) == "[]") return true;
    return false;
}

// Single source of truth for the overloadable operator symbols recognized by `op<sym>`.
// Maps the leading TokenType to its canonical string form used in method names
// (e.g. "op" + "+=" → "op+="). Multi-token forms ([] and []=) are handled inline
// in the helpers below.
static const std::map<TokenType, std::string> kOpSymbols = {
    {TokenType::kEquals,    "="},
    {TokenType::kArrowLeft, "<-"},
    {TokenType::kArrowBoth, "<->"},
    {TokenType::kPlus,      "+"},
    {TokenType::kMinus,     "-"},
    {TokenType::kStar,      "*"},
    {TokenType::kSlash,     "/"},
    {TokenType::kPercent,   "%"},
    {TokenType::kBitAnd,    "&"},
    {TokenType::kBitOr,     "|"},
    {TokenType::kBitXor,    "^"},
    {TokenType::kLShift,    "<<"},
    {TokenType::kRShift,    ">>"},
    {TokenType::kAnd,       "&&"},
    {TokenType::kOr,        "||"},
    {TokenType::kXorXor,    "^^"},
    {TokenType::kPlusEq,    "+="},
    {TokenType::kMinusEq,   "-="},
    {TokenType::kStarEq,    "*="},
    {TokenType::kSlashEq,   "/="},
    {TokenType::kPercentEq, "%="},
    {TokenType::kBitAndEq,  "&="},
    {TokenType::kBitOrEq,   "|="},
    {TokenType::kBitXorEq,  "^="},
    {TokenType::kLShiftEq,  "<<="},
    {TokenType::kRShiftEq,  ">>="},
    {TokenType::kAndEq,     "&&="},
    {TokenType::kOrEq,      "||="},
    {TokenType::kXorXorEq,  "^^="},
    {TokenType::kEqEq,      "=="},
    {TokenType::kNotEq,     "!="},
    {TokenType::kLt,        "<"},
    {TokenType::kGt,        ">"},
    {TokenType::kLtEq,      "<="},
    {TokenType::kGtEq,      ">="},
    {TokenType::kBitNot,    "~"},
    {TokenType::kNot,       "!"},
};

std::optional<std::string> Parser::peekOpSymbolAt(int offset) {
    int i = pos_ + offset;
    if (i >= (int)tokens_.size()) return std::nullopt;
    TokenType t = tokens_[i].type;
    // multi-token forms
    if (t == TokenType::kBracketAssign) return std::string("[]=");
    if (t == TokenType::kLBracket
        && i + 1 < (int)tokens_.size()
        && tokens_[i + 1].type == TokenType::kRBracket) {
        return std::string("[]");
    }
    auto it = kOpSymbols.find(t);
    if (it == kOpSymbols.end()) return std::nullopt;
    return it->second;
}

void Parser::checkOpArity(const std::string& op_name, int actual, int op_tok) {
    // Allowed explicit-parameter counts per in-class op<sym>. self is implicit.
    // Unary + and - accept arity 0 (self-only), 1 (unary), 2 (binary).
    // Unary ~ and ! accept arity 0 (self-only), 1 (unary). No binary form.
    static const std::map<std::string, std::vector<int>> arity = {
        {"op=", {1}}, {"op<-", {1}}, {"op<->", {1}},
        {"op+", {0, 1, 2}}, {"op-", {0, 1, 2}},
        {"op*", {2}}, {"op/", {2}}, {"op%", {2}},
        {"op&", {2}}, {"op|", {2}}, {"op^", {2}}, {"op<<", {2}}, {"op>>", {2}},
        {"op&&", {2}}, {"op||", {2}}, {"op^^", {2}},
        {"op~", {0, 1}}, {"op!", {0, 1}},
        {"op+=", {1}}, {"op-=", {1}}, {"op*=", {1}}, {"op/=", {1}}, {"op%=", {1}},
        {"op&=", {1}}, {"op|=", {1}}, {"op^=", {1}}, {"op<<=", {1}}, {"op>>=", {1}},
        {"op&&=", {1}}, {"op||=", {1}}, {"op^^=", {1}},
        {"op==", {1}}, {"op!=", {1}}, {"op<", {1}}, {"op>", {1}}, {"op<=", {1}}, {"op>=", {1}},
        {"op[]", {1}}, {"op[]=", {2}},
    };
    auto it = arity.find(op_name);
    if (it == arity.end()) return;
    const auto& allowed = it->second;
    for (int n : allowed) if (n == actual) return;
    std::string list;
    for (size_t i = 0; i < allowed.size(); ++i) {
        if (i > 0) list += (i + 1 == allowed.size()) ? " or " : ", ";
        list += std::to_string(allowed[i]);
    }
    errorAt(op_tok, "operator '" + op_name + "' requires "
        + list + " parameter" + (allowed.size() == 1 && allowed[0] == 1 ? "" : "s")
        + "; got " + std::to_string(actual));
}

void Parser::checkOpMutable(const std::string& op_name,
                            const std::vector<std::pair<std::string,std::string>>& params,
                            const std::vector<bool>& param_mutable,
                            int op_tok) {
    if (op_name != "op<-" && op_name != "op<->") return;
    for (size_t i = 0; i < params.size(); ++i) {
        if (isParamIndirectType(params[i].first) && !param_mutable[i]) {
            errorAt(op_tok, op_name + " pointer param '" + params[i].second
                + "' requires 'mutable'");
        }
    }
}

std::optional<std::string> Parser::consumeOpSymbol() {
    if (pos_ >= (int)tokens_.size()) return std::nullopt;
    TokenType t = tokens_[pos_].type;
    if (t == TokenType::kBracketAssign) {
        advance();
        return std::string("[]=");
    }
    if (t == TokenType::kLBracket
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kRBracket) {
        advance(); // [
        advance(); // ]
        return std::string("[]");
    }
    auto it = kOpSymbols.find(t);
    if (it == kOpSymbols.end()) return std::nullopt;
    advance();
    return it->second;
}

bool Parser::isTemplateCallLookahead() const {
    [[maybe_unused]] int t_start = pos_;
    // pos_ points at '<'; scan forward to see if this is name<Type,...>(
    // Valid: tokens inside <> are type keywords, uppercase idents, ^, [], commas
    int i = pos_ + 1;
    while (i < (int)tokens_.size()) {
        const Token& ti = tokens_[i];
        if (ti.type == TokenType::kGt) {
            // found closing >; next must be (
            return (i + 1 < (int)tokens_.size())
                && tokens_[i + 1].type == TokenType::kLParen;
        }
        // allow type-name tokens and commas inside the angle brackets
        bool ok = isTypeName(ti)
               || isUserTypeName(ti)
               || ti.type == TokenType::kComma
               || ti.type == TokenType::kBitXor
               || ti.type == TokenType::kLBracket
               || ti.type == TokenType::kRBracket;
        if (!ok) return false;
        i++;
    }
    return false;
}

bool Parser::isInstantiationLookahead() const {
    [[maybe_unused]] int t_start = pos_;
    // pos_ is at identifier; pos_+1 is '<'; scan type args and require '>' followed by
    // '(...)' (param signature) followed by ';'. Distinguishes from a class-template
    // definition `Name<T>(fields) { ... }` which has '{' after the parens, not ';'.
    int i = pos_ + 2;
    while (i < (int)tokens_.size()) {
        const Token& ti = tokens_[i];
        if (ti.type == TokenType::kGt) break;
        bool ok = isTypeName(ti) || isUserTypeName(ti)
               || ti.type == TokenType::kComma
               || ti.type == TokenType::kBitXor
               || ti.type == TokenType::kLBracket
               || ti.type == TokenType::kRBracket;
        if (!ok) return false;
        i++;
    }
    if (i >= (int)tokens_.size() || tokens_[i].type != TokenType::kGt) return false;
    i++; // past '>'
    if (i >= (int)tokens_.size() || tokens_[i].type != TokenType::kLParen) return false;
    int depth = 1;
    i++;
    while (i < (int)tokens_.size() && depth > 0) {
        if (tokens_[i].type == TokenType::kLParen) depth++;
        else if (tokens_[i].type == TokenType::kRParen) depth--;
        i++;
    }
    if (depth != 0) return false;
    return i < (int)tokens_.size() && tokens_[i].type == TokenType::kSemicolon;
}

bool Parser::isTemplateTypeArgLookahead() const {
    [[maybe_unused]] int t_start = pos_;
    // pos_ is at '<'; scan forward to see if this is a template type-arg list.
    // Valid contents: type keywords, uppercase idents, ^, [], commas.
    int i = pos_ + 1;
    while (i < (int)tokens_.size()) {
        const Token& ti = tokens_[i];
        if (ti.type == TokenType::kGt) return true; // found closing '>'
        bool ok = isTypeName(ti) || isUserTypeName(ti)
               || ti.type == TokenType::kComma
               || ti.type == TokenType::kBitXor
               || ti.type == TokenType::kLBracket
               || ti.type == TokenType::kRBracket;
        if (!ok) return false;
        i++;
    }
    return false;
}

std::string Parser::parseTypeName() {
    [[maybe_unused]] int t_start = pos_;
    std::string base;
    // anon-tuple type: (t1, t2, ...) — may carry trailing ^ or []
    if (peek().type == TokenType::kLParen) {
        advance(); // consume '('
        base = "(";
        bool first = true;
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            if (!first) base += ",";
            first = false;
            base += parseTypeName();
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')' in anon-tuple type");
        base += ")";
        // fall through to the trailing ^/[] loop below
        while (true) {
            if (peek().type == TokenType::kBitXor) { advance(); base += "^"; }
            else if (peek().type == TokenType::kLBracket
                       && pos_ + 1 < (int)tokens_.size()
                       && tokens_[pos_ + 1].type == TokenType::kRBracket) {
                advance(); advance();
                base += "[]";
            }
            else break;
        }
        return base;
    }
    if (isTypeName(peek())) base = advance().value;
    else if (isUserTypeName(peek())) base = advance().value;
    else errorHere("expected type name, got " + tokenLabel(peek()));
    // qualified nested-type suffix: Outer:Inner — consume only when the next-next token is
    // an identifier (the variable name), not a '(' (which would be an external-method def).
    if (peek().type == TokenType::kColon
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kIdentifier
        && pos_ + 2 < (int)tokens_.size()
        && tokens_[pos_ + 2].type != TokenType::kLParen) {
        advance(); // consume ':'
        std::string inner = advance().value;
        base += "." + inner;
    } else {
        // unqualified — apply nested-alias map (e.g. "Inner" → "Outer.Inner" inside Outer's body)
        auto it = nested_alias_.find(base);
        if (it != nested_alias_.end()) base = it->second;
    }
    // template type instantiation in type position: Name<Type> → mangled as Name__Type
    if (peek().type == TokenType::kLt && isTemplateTypeArgLookahead()) {
        advance(); // consume '<'
        while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
            std::string ta = parseTypeName();
            base += "__" + ta;
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kGt, "expected '>' after template type args");
    }
    // consume trailing ^ or [] for pointer/reference types — kept distinct:
    //   ^ = reference (no arithmetic allowed)
    //   [] = pointer  (arithmetic allowed: ++, --, +, -)
    while (true) {
        if (peek().type == TokenType::kBitXor) {
            advance();
            base += "^";
        } else if (peek().type == TokenType::kLBracket
                   && pos_ + 1 < (int)tokens_.size()
                   && tokens_[pos_ + 1].type == TokenType::kRBracket) {
            advance(); advance(); // consume [ ]
            base += "[]";
        } else {
            break;
        }
    }
    return base;
}

// --- Expression parsing ---

std::unique_ptr<Expr> Parser::parsePrimary() {
    [[maybe_unused]] int t_start = pos_;
    Token t = peek();
    if (t.type == TokenType::kIntLiteral) {
        advance();
        return make<IntLiteralExpr>(t_start, std::stoll(t.value));
    }
    if (t.type == TokenType::kUintLiteral) {
        advance();
        uint64_t uval = std::stoull(t.value);
        return make<IntLiteralExpr>(t_start, static_cast<int64_t>(uval), false, true);
    }
    if (t.type == TokenType::kCharLiteral) {
        advance();
        return make<IntLiteralExpr>(t_start, static_cast<int>(std::stoll(t.value)), /*is_char=*/true);
    }
    if (t.type == TokenType::kFloatLiteral) {
        advance();
        return make<FloatLiteralExpr>(t_start, std::stod(t.value));
    }
    // type conversion: primitive_type(expr) — legacy form
    if (isTypeName(t) && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kLParen) {
        std::string type_name = advance().value; // consume type keyword
        advance(); // consume '('
        auto operand = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        return make<TypeConvExpr>(t_start, type_name, std::move(operand));
    }
    // chainable type conversion without outer parens: int=expr, float32=expr, etc.
    // type keywords can't be variable names, so this is unambiguous in expression position
    if (isTypeName(t) && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kEquals) {
        std::string type_name = advance().value; // consume type keyword
        advance(); // consume '='
        auto operand = parseExpr();
        return make<TypeConvExpr>(t_start, type_name, std::move(operand));
    }
    if (t.type == TokenType::kStringLiteral) {
        advance();
        std::string value = t.value;
        while (peek().type == TokenType::kStringLiteral)
            value += advance().value;
        return make<StringLiteralExpr>(t_start, value);
    }
    if (t.type == TokenType::kTrue)  { advance(); return make<IntLiteralExpr>(t_start, 1); }
    if (t.type == TokenType::kFalse) { advance(); return make<IntLiteralExpr>(t_start, 0); }
    if (t.type == TokenType::kNullptr) { advance(); return make<NullptrExpr>(t_start); }
    if (t.type == TokenType::kNew) {
        advance();
        // placement new: new(addr) Type(args)
        if (peek().type == TokenType::kLParen) {
            advance();
            auto addr = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            std::string elem_type = parseTypeName();
            std::vector<std::unique_ptr<Expr>> args;
            if (peek().type == TokenType::kLParen) {
                advance();
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "expected ')'");
            }
            return make<PlacementNewExpr>(t_start, std::move(addr), elem_type, std::move(args));
        }
        std::string elem_type = parseTypeName();
        if (peek().type == TokenType::kLBracket) {
            advance();
            auto count = parseExpr();
            expect(TokenType::kRBracket, "expected ']'");
            return make<NewExpr>(t_start, elem_type, std::move(count));
        } else if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            return make<NewScalarExpr>(t_start, elem_type, std::move(args));
        } else {
            // new Type; — equivalent to new Type()
            return make<NewScalarExpr>(t_start, elem_type, std::vector<std::unique_ptr<Expr>>{});
        }
    }
    if (t.type == TokenType::kSizeof) {
        advance();
        expect(TokenType::kLParen, "expected '(' after sizeof");
        auto se = make<SizeofExpr>(t_start);
        // Built-in type keywords can't be parsed as expressions, so route them
        // through parseTypeName() and carry the result as a VarExpr name.
        // All other forms (user identifiers, variables) parse as expressions
        // and are disambiguated in codegen against the symbol table.
        if (isTypeName(peek())) {
            se->operand = make<VarExpr>(t_start, parseTypeName());
        } else {
            se->operand = parseExpr();
        }
        expect(TokenType::kRParen, "expected ')'");
        return se;
    }
    if (t.type == TokenType::kColonColon) {
        advance(); // consume '::'
        std::string fn = expect(TokenType::kIdentifier, "expected name after '::'").value;
        expect(TokenType::kLParen, "expected '(' after '::name'");
        std::vector<std::unique_ptr<Expr>> args;
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            args.push_back(parseExpr());
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')'");
        auto call = make<CallExpr>(t_start, fn, std::move(args));
        call->qualifier = "::";
        return call;
    }
    if (t.type == TokenType::kSelf) {
        advance();
        return make<VarExpr>(t_start, t.value);
    }
    if (t.type == TokenType::kIdentifier) {
        advance();
        // qualified call: Name:method(args) — namespace function call.
        // Suppressed in contexts where ':' terminates the expression (e.g. case labels).
        if (colon_terminates_expr_ == 0
            && peek().type == TokenType::kColon
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIdentifier
            && pos_ + 2 < (int)tokens_.size()
            && tokens_[pos_ + 2].type == TokenType::kLParen) {
            advance(); // consume ':'
            std::string method = advance().value; // consume method identifier
            advance(); // consume '('
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            auto call = make<CallExpr>(t_start, method, std::move(args));
            call->qualifier = t.value;
            return call;
        }
        // template call: name<TypeArg,...>(args)
        if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
            advance(); // consume '<'
            std::vector<std::string> type_args;
            while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
                type_args.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kGt, "expected '>'");
            expect(TokenType::kLParen, "expected '('");
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            auto call = make<CallExpr>(t_start, t.value, std::move(args));
            call->type_args = std::move(type_args);
            return call;
        }
        if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            return make<CallExpr>(t_start, t.value, std::move(args));
        }
        return make<VarExpr>(t_start, t.value);
    }
    // type conversion expression: (Type=expr)
    // distinguished from grouped expression by TypeName immediately followed by =
    if (t.type == TokenType::kLParen
        && pos_ + 1 < (int)tokens_.size()
        && (isTypeName(tokens_[pos_ + 1]) || isUserTypeName(tokens_[pos_ + 1]))) {
        int saved = pos_;
        advance(); // consume '('
        std::string type_name = parseTypeName();
        if (peek().type == TokenType::kEquals) {
            advance(); // consume '='
            auto operand = parseExpr();
            expect(TokenType::kRParen, "expected ')' after type conversion");
            return make<TypeConvExpr>(t_start, type_name, std::move(operand));
        }
        pos_ = saved; // not a type conversion — backtrack and fall through
    }
    if (t.type == TokenType::kLParen) {
        advance();
        auto first = parseExpr();
        if (peek().type == TokenType::kComma) {
            auto tuple = make<TupleExpr>(t_start);
            tuple->values.push_back(std::move(first));
            while (peek().type == TokenType::kComma) {
                advance();
                tuple->values.push_back(parseExpr());
            }
            expect(TokenType::kRParen, "expected ')'");
            return tuple;
        }
        expect(TokenType::kRParen, "expected ')'");
        return first;
    }
    if (t.type == TokenType::kHashHash) {
        int src_tok = pos_;
        advance();
        std::string kw = expect(TokenType::kIdentifier,
            "expected name, type, line, file, func, date, or time after ##").value;
        if (kw == "name" || kw == "type") {
            expect(TokenType::kLParen, "expected '(' after ##" + kw);
            auto operand = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            return make<StringifyExpr>(src_tok, kw, std::move(operand));
        }
        if (kw == "line" || kw == "file" || kw == "func" || kw == "date" || kw == "time")
            return make<StringifyExpr>(src_tok, kw, nullptr);
        errorAt(src_tok, "unknown ## operator '" + kw + "'");
    }
    errorHere("expected expression, got " + tokenLabel(t));
}

// postfix: handle .field and .method(args) chaining
std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> base) {
    [[maybe_unused]] int t_start = pos_;
    while (true) {
        if (peek().type == TokenType::kDot) {
            advance();
            // allow sizeof and ~ as method names
            std::string member;
            if (peek().type == TokenType::kSizeof)  { advance(); member = "sizeof"; }
            else if (peek().type == TokenType::kBitNot) { advance(); member = "~"; }
            else member = expect(TokenType::kIdentifier, "expected field or method name").value;
            if (peek().type == TokenType::kLParen) {
                advance();
                std::vector<std::unique_ptr<Expr>> args;
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "expected ')'");
                base = make<MethodCallExpr>(t_start, std::move(base), member, std::move(args));
            } else {
                base = make<FieldAccessExpr>(t_start, std::move(base), member);
            }
        } else if (peek().type == TokenType::kLBracket) {
            advance();
            auto idx = parseExpr();
            if (peek().type == TokenType::kDotDot) {
                advance();
                auto end_expr = parseExpr();
                expect(TokenType::kRBracket, "expected ']'");
                base = make<SliceExpr>(t_start, std::move(base), std::move(idx), std::move(end_expr));
            } else {
                expect(TokenType::kRBracket, "expected ']'");
                base = make<ArrayIndexExpr>(t_start, std::move(base), std::move(idx));
            }
        } else if (peek().type == TokenType::kBitXor) {
            // postfix ^ is dereference only when NOT followed by an expression operand
            // (if followed by identifier/literal/lparen it's binary XOR, handled by parseBitXor)
            int next = pos_ + 1;
            TokenType ntt = (next < (int)tokens_.size()) ? tokens_[next].type : TokenType::kEof;
            bool next_is_operand =
                ntt == TokenType::kIdentifier || ntt == TokenType::kIntLiteral ||
                ntt == TokenType::kStringLiteral || ntt == TokenType::kLParen ||
                ntt == TokenType::kTrue || ntt == TokenType::kFalse;
            if (next_is_operand) break; // leave ^ for parseBitXor
            advance();
            base = make<DerefExpr>(t_start, std::move(base));
        } else if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
            std::string op = (peek().type == TokenType::kPlusPlus) ? "++" : "--";
            advance();
            // ptr++^ — post-inc/dec then deref: treat as PostIncDerefExpr
            if (peek().type == TokenType::kBitXor) {
                advance();
                base = make<PostIncDerefExpr>(t_start, std::move(base), op);
            } else {
                std::string uop = (op == "++") ? "post++" : "post--";
                base = make<UnaryExpr>(t_start, uop, std::move(base));
            }
        } else {
            break;
        }
    }
    return base;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    [[maybe_unused]] int t_start = pos_;
    // #x — desugar to (##type(x), ##name(x), ^x)
    if (peek().type == TokenType::kHash) {
        int src_tok = pos_;
        advance();
        auto operand = parsePostfix(parsePrimary());
        auto* ve = dynamic_cast<VarExpr*>(operand.get());
        if (!ve)
            errorAt(src_tok, "# requires a simple variable name");
        std::string name = ve->name;
        auto tuple = make<TupleExpr>(src_tok);
        tuple->values.push_back(
            make<StringifyExpr>(src_tok, "type", make<VarExpr>(src_tok, name)));
        tuple->values.push_back(
            make<StringifyExpr>(src_tok, "name", make<VarExpr>(src_tok, name)));
        tuple->values.push_back(make<AddrOfExpr>(src_tok, std::move(operand)));
        return tuple;
    }
    // pointer reinterpret cast: <Type^> expr  or  <Type[]> expr  or  <intptr> expr
    if (peek().type == TokenType::kLt) {
        int saved = pos_;
        advance(); // tentatively consume <
        if (isTypeName(peek()) || isUserTypeName(peek())) {
            try {
                std::string target = parseTypeName(); // consumes type + optional ^/[]
                if (peek().type == TokenType::kGt) {
                    advance(); // consume >
                    auto operand = parseUnary();
                    return make<PtrCastExpr>(t_start, target, std::move(operand));
                }
            } catch (...) {}
        }
        pos_ = saved; // not a cast — restore for comparison operator
    }
    if (peek().type == TokenType::kPlus) {
        advance();
        return make<UnaryExpr>(t_start, "+", parseUnary());
    }
    if (peek().type == TokenType::kMinus) {
        advance();
        return make<UnaryExpr>(t_start, "-", parseUnary());
    }
    if (peek().type == TokenType::kNot) {
        advance();
        return make<UnaryExpr>(t_start, "!", parseUnary());
    }
    if (peek().type == TokenType::kBitNot) {
        advance();
        return make<UnaryExpr>(t_start, "~", parseUnary());
    }
    // pre-increment/decrement: ++x, --x, ++(ptr^) — returns new value
    if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
        std::string op = (peek().type == TokenType::kPlusPlus) ? "pre++" : "pre--";
        advance();
        auto operand = parsePostfix(parsePrimary());  // handles ++(ref^), ++arr[i], etc.
        return make<UnaryExpr>(t_start, op, std::move(operand));
    }
    // prefix ^ — take address: ^x, ^arr[i][j]
    if (peek().type == TokenType::kBitXor) {
        advance();
        auto operand = parsePostfix(parsePrimary());
        return make<AddrOfExpr>(t_start, std::move(operand));
    }
    return parsePostfix(parsePrimary());
}

std::unique_ptr<Expr> Parser::parseMulDiv() {
    auto left = parseUnary();
    while (peek().type == TokenType::kStar  ||
           peek().type == TokenType::kSlash  ||
           peek().type == TokenType::kPercent) {
        int op_tok = pos_;
        std::string op = advance().value;
        left = make<BinaryExpr>(op_tok, op, std::move(left), parseUnary());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAddSub() {
    auto left = parseMulDiv();
    while (peek().type == TokenType::kPlus ||
           peek().type == TokenType::kMinus) {
        int op_tok = pos_;
        std::string op = advance().value;
        left = make<BinaryExpr>(op_tok, op, std::move(left), parseMulDiv());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseShift() {
    auto left = parseAddSub();
    while (peek().type == TokenType::kLShift ||
           peek().type == TokenType::kRShift) {
        int op_tok = pos_;
        std::string op = advance().value;
        left = make<BinaryExpr>(op_tok, op, std::move(left), parseAddSub());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseRelational() {
    auto left = parseShift();
    while (peek().type == TokenType::kLt   ||
           peek().type == TokenType::kGt   ||
           peek().type == TokenType::kLtEq ||
           peek().type == TokenType::kGtEq) {
        int op_tok = pos_;
        std::string op = advance().value;
        left = make<BinaryExpr>(op_tok, op, std::move(left), parseShift());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    auto left = parseRelational();
    while (peek().type == TokenType::kEqEq ||
           peek().type == TokenType::kNotEq) {
        int op_tok = pos_;
        std::string op = advance().value;
        left = make<BinaryExpr>(op_tok, op, std::move(left), parseRelational());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitAnd() {
    auto left = parseEquality();
    while (peek().type == TokenType::kBitAnd) {
        int op_tok = pos_;
        advance();
        left = make<BinaryExpr>(op_tok, "&", std::move(left), parseEquality());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitXor() {
    auto left = parseBitAnd();
    while (peek().type == TokenType::kBitXor) {
        int op_tok = pos_;
        advance();
        left = make<BinaryExpr>(op_tok, "^", std::move(left), parseBitAnd());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitOr() {
    auto left = parseBitXor();
    while (peek().type == TokenType::kBitOr) {
        int op_tok = pos_;
        advance();
        left = make<BinaryExpr>(op_tok, "|", std::move(left), parseBitXor());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto left = parseBitOr();
    while (peek().type == TokenType::kAnd) {
        int op_tok = pos_;
        advance();
        left = make<BinaryExpr>(op_tok, "&&", std::move(left), parseBitOr());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseExpr() {
    auto left = parseLogicalAnd();
    while (peek().type == TokenType::kOr ||
           peek().type == TokenType::kXorXor) {
        int op_tok = pos_;
        std::string op = advance().value;
        left = make<BinaryExpr>(op_tok, op, std::move(left), parseLogicalAnd());
    }
    return left;
}

// --- Statement parsing ---

std::unique_ptr<BlockStmt> Parser::parseBlock(std::vector<std::string> predeclare) {
    [[maybe_unused]] int t_start = pos_;
    expect(TokenType::kLBrace, "expected '{'");
    scope_stack_.push_back({});
    for (auto& n : predeclare)
        declareVar(n, t_start);
    auto block = make<BlockStmt>(t_start);
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof)
        block->stmts.push_back(parseStmt());
    // (P1) nested function uniqueness within this block — same name + signature.
    {
        std::set<std::string> sigs;
        for (auto& st : block->stmts) {
            auto* nf = dynamic_cast<NestedFunctionDefStmt*>(st.get());
            if (!nf) continue;
            std::string key = nf->def.name + "(";
            for (auto& p : nf->def.params) key += p.first + ",";
            key += ")";
            if (!sigs.insert(key).second) {
                throw CompileError{-1, 0, std::string("nested function '" + nf->def.name
                    + "' redefined with same signature in same block")};
            }
        }
    }
    scope_stack_.pop_back();
    expect(TokenType::kRBrace, "expected '}'");
    return block;
}

// Normalize DerefExpr(UnaryExpr("post++"|"post--", VarExpr)) to PostIncDerefExpr,
// so that paren-wrapped post-inc-deref `(p++)^` produces the same AST as the
// unparenthesized `p++^`. Caller passes ownership; receives ownership back
// (possibly the same object if no rewrite was needed).
static std::unique_ptr<Expr> normalizePostIncDeref(std::unique_ptr<Expr> e) {
    auto* de = dynamic_cast<DerefExpr*>(e.get());
    if (!de) return e;
    auto* ue = dynamic_cast<UnaryExpr*>(de->operand.get());
    if (!ue || (ue->op != "post++" && ue->op != "post--")) return e;
    auto* ve = dynamic_cast<VarExpr*>(ue->operand.get());
    if (!ve) return e;
    std::string op = (ue->op == "post++") ? "++" : "--";
    auto var = std::make_unique<VarExpr>(ve->name);
    var->file_id = ve->file_id; var->tok = ve->tok;
    auto pid = std::make_unique<PostIncDerefExpr>(std::move(var), op);
    pid->file_id = ve->file_id; pid->tok = ve->tok;
    return pid;
}

std::unique_ptr<Stmt> Parser::buildAssignFromLhs(
        std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs, bool is_move, int op_tok) {
    int t_start = lhs->tok;
    lhs = normalizePostIncDeref(std::move(lhs));

    if (auto* ve = dynamic_cast<VarExpr*>(lhs.get())) {
        std::string name = ve->name;
        // `name != "self"` carve-out: `self = expr;` parses as
        // VarExpr(name="self") on the LHS (kSelf primary arm produces a
        // VarExpr). Without this clause, the path below would treat self as
        // an inferred-type new local declaration. self is implicit, never a
        // declared local — fall through to AssignStmt instead.
        if (!isInScope(name) && !current_slid_fields_.count(name) && name != "self") {
            declareVar(name, ve->tok);
            // Track tuple-typed locals so the short-form for-loop can iterate
            // them by synthesizing per-slot ArrayIndexExpr accesses.
            if (auto* te = dynamic_cast<TupleExpr*>(rhs.get())) {
                if (auto* li = findLocal(name)) {
                    li->is_tuple = true;
                    li->tuple_count = (int)te->values.size();
                }
            }
            return make<VarDeclStmt>(t_start, "", name, std::move(rhs),
                std::vector<std::unique_ptr<Expr>>{}, is_move);
        }
        return make<AssignStmt>(t_start, name, std::move(rhs), is_move);
    }
    if (auto* pide = dynamic_cast<PostIncDerefExpr*>(lhs.get())) {
        return make<PostIncDerefAssignStmt>(t_start,
            std::move(pide->operand), pide->op, std::move(rhs), is_move);
    }
    if (auto* de = dynamic_cast<DerefExpr*>(lhs.get())) {
        return make<DerefAssignStmt>(t_start, std::move(de->operand), std::move(rhs), is_move);
    }
    if (auto* fa = dynamic_cast<FieldAccessExpr*>(lhs.get())) {
        return make<FieldAssignStmt>(t_start,
            std::move(fa->object), fa->field, std::move(rhs), is_move);
    }
    if (auto* ai = dynamic_cast<ArrayIndexExpr*>(lhs.get())) {
        return make<IndexAssignStmt>(t_start,
            std::move(ai->base), std::move(ai->index), std::move(rhs), is_move);
    }
    errorAt(op_tok, "invalid assignment target");
}

std::unique_ptr<Stmt> Parser::buildSwapFromLhs(
        std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs) {
    int t_start = lhs->tok;
    lhs = normalizePostIncDeref(std::move(lhs));
    rhs = normalizePostIncDeref(std::move(rhs));
    return make<SwapStmt>(t_start, std::move(lhs), std::move(rhs));
}

std::unique_ptr<Stmt> Parser::buildCompoundAssignFromLhs(
        std::unique_ptr<Expr> lhs, const std::string& op,
        std::unique_ptr<Expr> rhs, int op_tok) {
    lhs = normalizePostIncDeref(std::move(lhs));
    if (!dynamic_cast<VarExpr*>(lhs.get())
        && !dynamic_cast<DerefExpr*>(lhs.get())
        && !dynamic_cast<FieldAccessExpr*>(lhs.get())
        && !dynamic_cast<ArrayIndexExpr*>(lhs.get())
        && !dynamic_cast<PostIncDerefExpr*>(lhs.get())) {
        errorAt(op_tok, "compound assignment requires an lvalue");
    }
    return make<CompoundAssignStmt>(op_tok,
        std::move(lhs), op, std::move(rhs));
}

std::unique_ptr<Stmt> Parser::parseLvalueTail(std::unique_ptr<Expr> lhs) {
    [[maybe_unused]] int t_start = pos_;
    static const std::map<TokenType, std::string> compound_ops = {
        {TokenType::kPlusEq,    "+"},
        {TokenType::kMinusEq,   "-"},
        {TokenType::kStarEq,    "*"},
        {TokenType::kSlashEq,   "/"},
        {TokenType::kPercentEq, "%"},
        {TokenType::kBitAndEq,  "&"},
        {TokenType::kBitOrEq,   "|"},
        {TokenType::kBitXorEq,  "^"},
        {TokenType::kLShiftEq,  "<<"},
        {TokenType::kRShiftEq,  ">>"},
        {TokenType::kAndEq,     "&&"},
        {TokenType::kOrEq,      "||"},
        {TokenType::kXorXorEq,  "^^"},
    };
    auto cop = compound_ops.find(peek().type);
    if (cop != compound_ops.end()) {
        int op_tok = pos_;
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return buildCompoundAssignFromLhs(std::move(lhs), cop->second, std::move(rhs), op_tok);
    }
    if (peek().type == TokenType::kEquals) {
        int op_tok = pos_;
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return buildAssignFromLhs(std::move(lhs), std::move(rhs), false, op_tok);
    }
    if (peek().type == TokenType::kArrowLeft) {
        int op_tok = pos_;
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return buildAssignFromLhs(std::move(lhs), std::move(rhs), true, op_tok);
    }
    if (peek().type == TokenType::kArrowBoth) {
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return buildSwapFromLhs(std::move(lhs), std::move(rhs));
    }
    expect(TokenType::kSemicolon, "expected ';'");
    if (auto* mc = dynamic_cast<MethodCallExpr*>(lhs.get())) {
        return make<MethodCallStmt>(t_start, 
            std::move(mc->object), mc->method, std::move(mc->args));
    }
    return make<ExprStmt>(t_start, std::move(lhs));
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    [[maybe_unused]] int t_start = pos_;
    Token t = peek();

    // global-qualified call statement: ::name(args);
    if (t.type == TokenType::kColonColon) {
        auto call = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return make<ExprStmt>(t_start, std::move(call));
    }

    if (t.type == TokenType::kDelete) {
        advance();
        auto operand = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return make<DeleteStmt>(t_start, std::move(operand));
    }

    if (t.type == TokenType::kReturn) {
        advance();
        if (peek().type == TokenType::kSemicolon) {
            advance();
            return make<ReturnStmt>(t_start, nullptr);
        }
        auto expr = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return make<ReturnStmt>(t_start, std::move(expr));
    }

    if (t.type == TokenType::kBreak) {
        advance();
        auto stmt = make<BreakStmt>(t_start);
        if (peek().type == TokenType::kIntLiteral) {
            stmt->number = std::stoi(advance().value);
        } else if (peek().type == TokenType::kIdentifier
                || peek().type == TokenType::kFor
                || peek().type == TokenType::kWhile
                || peek().type == TokenType::kSwitch) {
            stmt->label = advance().value;
        }
        expect(TokenType::kSemicolon, "expected ';'");
        return stmt;
    }

    if (t.type == TokenType::kContinue) {
        advance();
        auto stmt = make<ContinueStmt>(t_start);
        if (peek().type == TokenType::kIntLiteral) {
            stmt->number = std::stoi(advance().value);
        } else if (peek().type == TokenType::kIdentifier
                || peek().type == TokenType::kFor
                || peek().type == TokenType::kWhile
                || peek().type == TokenType::kSwitch) {
            stmt->label = advance().value;
        }
        expect(TokenType::kSemicolon, "expected ';'");
        return stmt;
    }

    // bare new-expression as a statement: new T(args);  new T[n];  new(addr) T(args);
    // result is discarded; constructor side-effects still run.
    if (t.type == TokenType::kNew) {
        auto e = parsePostfix(parsePrimary());
        expect(TokenType::kSemicolon, "expected ';'");
        return make<ExprStmt>(t_start, std::move(e));
    }

    // pre-increment/decrement statement: ++x;  ++ref^;  ++(ref^);
    if (t.type == TokenType::kPlusPlus || t.type == TokenType::kMinusMinus) {
        std::string op = (t.type == TokenType::kPlusPlus) ? "pre++" : "pre--";
        advance();
        auto operand = parsePostfix(parsePrimary());
        expect(TokenType::kSemicolon, "expected ';'");
        return make<ExprStmt>(t_start, 
            make<UnaryExpr>(t_start, op, std::move(operand)));
    }

    // naked block: { stmt; stmt; ... }
    if (t.type == TokenType::kLBrace) {
        return parseBlock();
    }

    if (t.type == TokenType::kIf) {
        advance();
        auto stmt = make<IfStmt>(t_start);
        expect(TokenType::kLParen, "expected '('");
        stmt->cond = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        stmt->then_block = parseBlock();
        if (peek().type == TokenType::kElse) {
            advance();
            if (peek().type == TokenType::kIf) {
                auto else_block = make<BlockStmt>(t_start);
                else_block->stmts.push_back(parseStmt());
                stmt->else_block = std::move(else_block);
            } else {
                stmt->else_block = parseBlock();
            }
        }
        return stmt;
    }

    if (t.type == TokenType::kWhile) {
        advance();
        auto stmt = make<WhileStmt>(t_start);
        if (peek().type == TokenType::kLBrace) {
            // bottom-condition while: while { body } :label (cond);
            stmt->bottom_condition = true;
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
            } else {
                stmt->block_label = "while";
            }
            expect(TokenType::kLParen, "expected '('");
            stmt->cond = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
        } else {
            expect(TokenType::kLParen, "expected '('");
            if (peek().type == TokenType::kRParen)
                stmt->cond = make<IntLiteralExpr>(t_start, 1);  // while () == while (true)
            else
                stmt->cond = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
                expect(TokenType::kSemicolon, "expected ';'");
            } else {
                stmt->block_label = "while";
            }
        }
        return stmt;
    }

    if (t.type == TokenType::kSwitch) {
        return parseSwitchStmt();
    }

    if (t.type == TokenType::kFor) {
        advance();
        // Both short and long forms start with '('. Discriminate by peeking
        // past the '(' for the short-form patterns:
        //   ( IDENT :          → short, no var-type
        //   ( TYPE IDENT :     → short, with var-type
        // Long-form init slots use '=' (or are empty), so ':' after the
        // identifier is the discriminator.
        if (peek().type == TokenType::kLParen) {
            int s = pos_ + 1; // first token after '('
            bool short_form = false;
            if (s < (int)tokens_.size()
                && (isTypeName(tokens_[s]) || isUserTypeName(tokens_[s]))) {
                if (s + 1 < (int)tokens_.size()
                    && tokens_[s + 1].type == TokenType::kColon) {
                    short_form = true;  // ( IDENT :
                } else {
                    // Walk past a type form (base name + optional qualifier,
                    // template args, and ^/[] suffix), then look for IDENT ':'.
                    int i = s + 1;
                    if (i + 1 < (int)tokens_.size()
                        && tokens_[i].type == TokenType::kColon
                        && tokens_[i + 1].type == TokenType::kIdentifier) {
                        i += 2;
                    }
                    if (i < (int)tokens_.size()
                        && tokens_[i].type == TokenType::kLt) {
                        int depth = 1;
                        i++;
                        while (i < (int)tokens_.size() && depth > 0
                                && tokens_[i].type != TokenType::kEof) {
                            if (tokens_[i].type == TokenType::kLt) depth++;
                            else if (tokens_[i].type == TokenType::kGt) depth--;
                            i++;
                        }
                    }
                    if (i < (int)tokens_.size()
                        && tokens_[i].type == TokenType::kBitXor) {
                        i++;
                    } else if (i + 1 < (int)tokens_.size()
                            && tokens_[i].type == TokenType::kLBracket
                            && tokens_[i + 1].type == TokenType::kRBracket) {
                        i += 2;
                    }
                    if (i + 1 < (int)tokens_.size()
                            && tokens_[i].type == TokenType::kIdentifier
                            && tokens_[i + 1].type == TokenType::kColon) {
                        short_form = true;  // ( TYPE [^|[]] IDENT :
                    }
                }
            }

            if (short_form) {
                expect(TokenType::kLParen, "expected '('");

                // for-scope: covers the desugared init/cond/update + body.
                // Loop var, synthesized helpers, and body locals all unwind here.
                scope_stack_.push_back({});

                int t_var_tok = pos_;
                std::string for_var_type, for_var_name;
                if (peek().type == TokenType::kColon) {
                    errorHere("expected variable name in for-iterator");
                } else if (isTypeName(peek())) {
                    for_var_type = parseTypeName();
                    t_var_tok = pos_;
                    for_var_name = expect(TokenType::kIdentifier,
                        "expected variable name").value;
                } else if (peek().type == TokenType::kIdentifier
                        && isVarDeclLookahead()) {
                    for_var_type = parseTypeName();
                    t_var_tok = pos_;
                    for_var_name = expect(TokenType::kIdentifier,
                        "expected variable name").value;
                } else {
                    t_var_tok = pos_;
                    for_var_name = expect(TokenType::kIdentifier,
                        "expected variable name").value;
                }
                expect(TokenType::kColon, "expected ':' in for-iterator");
                declareVar(for_var_name, t_var_tok);

                auto first_expr = parseExpr();

                auto stmt = make<ForLongStmt>(t_start);
                stmt->block_label = "for";

                // tiny synthesis helpers — all bound to t_start so any later
                // error attribution lands at the for keyword.
                auto _intLit = [&](int64_t n) -> std::unique_ptr<Expr> {
                    return make<IntLiteralExpr>(t_start, n);
                };
                auto _var = [&](const std::string& name) -> std::unique_ptr<Expr> {
                    return make<VarExpr>(t_start, name);
                };
                auto _bin = [&](const std::string& op,
                                 std::unique_ptr<Expr> l,
                                 std::unique_ptr<Expr> r) -> std::unique_ptr<Expr> {
                    return make<BinaryExpr>(t_start, op,
                        std::move(l), std::move(r));
                };
                auto _decl = [&](const std::string& type, const std::string& name,
                                  std::unique_ptr<Expr> init) -> std::unique_ptr<Stmt> {
                    return make<VarDeclStmt>(t_start, type, name,
                        std::move(init), std::vector<std::unique_ptr<Expr>>{}, false);
                };
                // Loop var decl — attributed to the loop-var token (so errors caret
                // there) and tagged so codegen can enforce the "explicit type for
                // class-typed iteration" rule.
                auto _loopDecl = [&](std::unique_ptr<Expr> init) -> std::unique_ptr<Stmt> {
                    auto d = make<VarDeclStmt>(t_var_tok, for_var_type, for_var_name,
                        std::move(init), std::vector<std::unique_ptr<Expr>>{}, false);
                    d->is_loop_var = true;
                    return d;
                };
                auto _loopDeclNoInit = [&]() -> std::unique_ptr<Stmt> {
                    auto d = make<VarDeclStmt>(t_var_tok, for_var_type, for_var_name,
                        nullptr, std::vector<std::unique_ptr<Expr>>{}, false);
                    d->is_loop_var = true;
                    return d;
                };
                auto _assign = [&](const std::string& name,
                                    std::unique_ptr<Expr> v) -> std::unique_ptr<Stmt> {
                    return make<AssignStmt>(t_start, name, std::move(v), false);
                };
                auto _consumeLabel = [&] {
                    if (peek().type == TokenType::kColon) {
                        advance();
                        stmt->block_label = expect(TokenType::kIdentifier,
                            "expected label name").value;
                        expect(TokenType::kSemicolon, "expected ';'");
                    }
                };
                auto _popScope = [&] {
                    scope_stack_.pop_back();
                };
                // Ref form: loop var is pointer-typed (Class^ or Class[]).
                // Switches the desugar to alias-into-source instead of copy.
                bool ref_form = !for_var_type.empty()
                    && (for_var_type.back() == '^'
                        || (for_var_type.size() >= 2
                            && for_var_type.substr(for_var_type.size() - 2) == "[]"));
                auto _addrOf = [&](std::unique_ptr<Expr> e) -> std::unique_ptr<Expr> {
                    return make<AddrOfExpr>(t_start, std::move(e));
                };
                // Index-iteration finisher — common to tuple-literal / tuple-named
                // / string-literal / fixed-size-array shapes. The caller has
                // populated init_stmts with whatever source-setup it needs (e.g.
                // capturing the tuple expression into a local), then calls this
                // with the source-name and the literal element count.
                //
                // Value form:
                //   init: T x [= src[0] if inferred type]; int $idx = 0;
                //   cond: $idx < count
                //   update: ++$idx
                //   body: x = src[$idx]; <user body>
                //
                // Ref form:
                //   init: T^ x = ^src[0]; int $idx = 0;
                //   cond: $idx < count
                //   update: ++$idx; x = ^src[$idx];
                //   body: <user body>
                auto _finishIter = [&](const std::string& source_name,
                                       std::unique_ptr<Expr> count_expr,
                                       bool is_iter_class) {
                    std::string idx_name =
                        "__$idx_" + std::to_string(synthetic_counter_++);
                    std::string end_name =
                        "__$end_" + std::to_string(synthetic_counter_++);
                    auto _srcAt = [&](std::unique_ptr<Expr> idx) -> std::unique_ptr<Expr> {
                        return make<ArrayIndexExpr>(t_start, _var(source_name),
                            std::move(idx));
                    };
                    std::unique_ptr<Stmt> loop_var_decl;
                    if (ref_form) {
                        loop_var_decl = _loopDecl(_addrOf(_srcAt(_intLit(0))));
                    } else if (for_var_type.empty()) {
                        // Inferred type: codegen reads elem type from src[0].
                        loop_var_decl = _loopDecl(_srcAt(_intLit(0)));
                    } else {
                        loop_var_decl = _loopDeclNoInit();
                    }
                    // Iterable-class iteration uses exactly-one-protocol detection
                    // to pick by-value vs by-ref; no further explicit-type rule
                    // applies, so suppress the predicate trigger on the loop var.
                    if (is_iter_class) {
                        if (auto* d = dynamic_cast<VarDeclStmt*>(loop_var_decl.get()))
                            d->is_loop_var = false;
                    }
                    stmt->init_stmts.push_back(std::move(loop_var_decl));
                    stmt->init_stmts.push_back(_decl("int", idx_name, _intLit(0)));
                    stmt->init_stmts.push_back(_decl("int", end_name, std::move(count_expr)));
                    stmt->cond = _bin("<", _var(idx_name), _var(end_name));
                    auto upd = make<BlockStmt>(t_start);
                    upd->stmts.push_back(_assign(idx_name,
                        _bin("+", _var(idx_name), _intLit(1))));
                    if (ref_form) {
                        upd->stmts.push_back(_assign(for_var_name,
                            _addrOf(_srcAt(_var(idx_name)))));
                    }
                    stmt->update_block = std::move(upd);
                    auto user_body = parseBlock();
                    auto new_body = make<BlockStmt>(t_start);
                    if (!ref_form) {
                        new_body->stmts.push_back(_assign(for_var_name,
                            _srcAt(_var(idx_name))));
                    }
                    for (auto& s : user_body->stmts)
                        new_body->stmts.push_back(std::move(s));
                    stmt->body = std::move(new_body);
                };

                // Range: `start .. [cmp] end [op step]`
                if (peek().type == TokenType::kDotDot) {
                    advance();
                    std::string cmp = "<";
                    if      (peek().type == TokenType::kLt)    { advance(); cmp = "<"; }
                    else if (peek().type == TokenType::kLtEq)  { advance(); cmp = "<="; }
                    else if (peek().type == TokenType::kGt)    { advance(); cmp = ">"; }
                    else if (peek().type == TokenType::kGtEq)  { advance(); cmp = ">="; }
                    else if (peek().type == TokenType::kNotEq) { advance(); cmp = "!="; }
                    auto end_expr = parseUnary();
                    std::string step_op = "+";
                    std::unique_ptr<Expr> step_expr;
                    TokenType nt = peek().type;
                    if (nt == TokenType::kPlus  || nt == TokenType::kMinus
                     || nt == TokenType::kStar  || nt == TokenType::kSlash) {
                        if      (nt == TokenType::kPlus)  step_op = "+";
                        else if (nt == TokenType::kMinus) step_op = "-";
                        else if (nt == TokenType::kStar)  step_op = "*";
                        else                              step_op = "/";
                        advance();
                        step_expr = parseUnary();
                    }
                    expect(TokenType::kRParen, "expected ')'");

                    std::string end_name = "__$end_" + std::to_string(synthetic_counter_++);
                    std::string step_name;
                    stmt->init_stmts.push_back(_loopDecl(std::move(first_expr)));
                    stmt->init_stmts.push_back(
                        _decl(for_var_type, end_name, std::move(end_expr)));
                    if (step_expr) {
                        step_name = "__$step_" + std::to_string(synthetic_counter_++);
                        stmt->init_stmts.push_back(
                            _decl(for_var_type, step_name, std::move(step_expr)));
                    }
                    stmt->cond = _bin(cmp, _var(for_var_name), _var(end_name));
                    std::unique_ptr<Expr> step_val =
                        step_name.empty() ? _intLit(1) : _var(step_name);
                    auto upd = make<BlockStmt>(t_start);
                    upd->stmts.push_back(_assign(for_var_name,
                        _bin(step_op, _var(for_var_name), std::move(step_val))));
                    stmt->update_block = std::move(upd);
                    stmt->body = parseBlock();
                    _consumeLabel();
                    _popScope();
                    return stmt;
                }

                // Tuple literal: capture the tuple expression once into a local,
                // then iterate it in place (variable-index on a homogeneous
                // anon-tuple is permitted at codegen).
                if (auto* te = dynamic_cast<TupleExpr*>(first_expr.get())) {
                    expect(TokenType::kRParen, "expected ')'");
                    int size = (int)te->values.size();
                    std::string src_name =
                        "__$src_" + std::to_string(synthetic_counter_++);
                    // `auto __$src = (...);` — codegen materializes the tuple
                    // into a fresh alloca and infers the anon-tuple type.
                    stmt->init_stmts.push_back(
                        _decl("", src_name, std::move(first_expr)));
                    _finishIter(src_name, _intLit(size), /*is_iter_class=*/false);
                    _consumeLabel();
                    _popScope();
                    return stmt;
                }

                // Enum: VarExpr whose name matches a known enum type.
                if (auto* ve = dynamic_cast<VarExpr*>(first_expr.get())) {
                    auto eit = enum_sizes_.find(ve->name);
                    if (eit != enum_sizes_.end()) {
                        int size = eit->second;
                        expect(TokenType::kRParen, "expected ')'");
                        stmt->init_stmts.push_back(_decl("int", for_var_name, _intLit(0)));
                        stmt->cond = _bin("<", _var(for_var_name), _intLit(size));
                        auto upd = make<BlockStmt>(t_start);
                        upd->stmts.push_back(_assign(for_var_name,
                            _bin("+", _var(for_var_name), _intLit(1))));
                        stmt->update_block = std::move(upd);
                        stmt->body = parseBlock();
                        _consumeLabel();
                        _popScope();
                        return stmt;
                    }
                }

                // String literal: bind to a `char[]` local once, then iterate.
                if (auto* sl = dynamic_cast<StringLiteralExpr*>(first_expr.get())) {
                    int s_len = (int)sl->value.size();
                    expect(TokenType::kRParen, "expected ')'");
                    std::string base_name =
                        "__$base_" + std::to_string(synthetic_counter_++);
                    stmt->init_stmts.push_back(
                        _decl("char[]", base_name, std::move(first_expr)));
                    _finishIter(base_name, _intLit(s_len), /*is_iter_class=*/false);
                    _consumeLabel();
                    _popScope();
                    return stmt;
                }

                // VarExpr: dispatch by parser-tracked kind.
                //   tuple-typed local → iterate in place (codegen allows
                //                       variable-index on homogeneous tuples)
                //   fixed-size array  → iterate by parser-known count
                //   else              → iterable class: pick by-value (op[]/size)
                //                       or by-reference (begin/end/next) by which
                //                       protocol the source's class defines.
                if (auto* ve = dynamic_cast<VarExpr*>(first_expr.get())) {
                    std::string iter_name = ve->name;
                    int tup_size = tupleSizeInScope(iter_name);
                    if (tup_size > 0) {
                        expect(TokenType::kRParen, "expected ')'");
                        _finishIter(iter_name, _intLit(tup_size), /*is_iter_class=*/false);
                        _consumeLabel();
                        _popScope();
                        return stmt;
                    }
                    int arr_count = arrayCountInScope(iter_name);
                    int arr_rank = arrayRankInScope(iter_name);
                    expect(TokenType::kRParen, "expected ')'");

                    if (arr_count > 0) {
                        if (arr_rank > 1)
                            errorAt(ve->tok, "short-form for: multi-dimensional fixed-size array iteration not supported");
                        _finishIter(iter_name, _intLit(arr_count), /*is_iter_class=*/false);
                    } else {
                        std::string src_type = typeInScope(iter_name);
                        auto mit = class_info_.find(src_type);
                        ClassInfo empty_ci;
                        const ClassInfo& ci = (mit != class_info_.end()) ? mit->second : empty_ci;
                        ProtocolDiag idx_diag = classifyByValue(ci, src_type);
                        ProtocolDiag ref_diag = classifyByRef(ci, src_type);
                        // Pick protocol. With both Good, explicit loop var
                        // type breaks the tie by matching one return; bare
                        // form requires explicit, identical returns are
                        // unbreakable. Per spec lines 25-27, a Bad protocol
                        // alongside a Good one is silently overlooked.
                        bool pick_by_value = false;
                        bool pick_by_ref = false;
                        if (idx_diag.status == ProtocolStatus::Good
                                && ref_diag.status == ProtocolStatus::Good) {
                            if (for_var_type.empty()) {
                                errorAt(ve->tok, "for-iterator: type '" + src_type
                                    + "' defines both op[]/size and begin/end/next; "
                                    "explicit loop var type required");
                            }
                            if (idx_diag.return_type == ref_diag.return_type) {
                                errorAt(ve->tok, "for-iterator: type '" + src_type
                                    + "' defines both op[]/size and begin/end/next "
                                    "with identical return types; cannot disambiguate");
                            }
                            if (for_var_type == idx_diag.return_type)        pick_by_value = true;
                            else if (for_var_type == ref_diag.return_type)   pick_by_ref = true;
                            else
                                errorAt(ve->tok, "for-iterator: loop var type '" + for_var_type
                                    + "' matches neither op[] return ('" + idx_diag.return_type
                                    + "') nor begin return ('" + ref_diag.return_type + "')");
                        } else if (idx_diag.status == ProtocolStatus::Good) {
                            pick_by_value = true;
                        } else if (ref_diag.status == ProtocolStatus::Good) {
                            pick_by_ref = true;
                        } else if (idx_diag.status == ProtocolStatus::Absent
                                && ref_diag.status == ProtocolStatus::Absent) {
                            errorAt(ve->tok, "for-iterator: type '" + src_type
                                + "' is not iterable: has neither op[]/size nor begin/end/next");
                        } else {
                            std::string msg = "for-iterator: ";
                            if (idx_diag.status == ProtocolStatus::Bad) msg += idx_diag.reason;
                            if (ref_diag.status == ProtocolStatus::Bad) {
                                if (idx_diag.status == ProtocolStatus::Bad) msg += "; ";
                                msg += ref_diag.reason;
                            }
                            errorAt(ve->tok, msg);
                        }
                        if (pick_by_value) {
                            // Lower to the same index/end-driven shape as
                            // tuple/array iteration; count is container.size().
                            std::vector<std::unique_ptr<Expr>> size_args;
                            std::unique_ptr<Expr> size_call(make<MethodCallExpr>(t_start,
                                _var(iter_name), "size", std::move(size_args)));
                            _finishIter(iter_name, std::move(size_call), /*is_iter_class=*/true);
                        } else if (pick_by_ref) {
                            // Loop var is the iterator; cond uses c.end();
                            // update calls c.next(iter).
                            std::string end_name =
                                "__$end_" + std::to_string(synthetic_counter_++);
                            std::vector<std::unique_ptr<Expr>> beg_args, end_args, next_args;
                            next_args.push_back(_var(for_var_name));
                            auto loop_var_decl = _loopDecl(
                                std::unique_ptr<Expr>(make<MethodCallExpr>(t_start,
                                    _var(iter_name), "begin", std::move(beg_args))));
                            if (auto* d = dynamic_cast<VarDeclStmt*>(loop_var_decl.get()))
                                d->is_loop_var = false;
                            stmt->init_stmts.push_back(std::move(loop_var_decl));
                            stmt->init_stmts.push_back(_decl(for_var_type, end_name,
                                std::unique_ptr<Expr>(make<MethodCallExpr>(t_start,
                                    _var(iter_name), "end", std::move(end_args)))));
                            stmt->cond = _bin("!=", _var(for_var_name), _var(end_name));
                            auto upd = make<BlockStmt>(t_start);
                            upd->stmts.push_back(_assign(for_var_name,
                                std::unique_ptr<Expr>(make<MethodCallExpr>(t_start,
                                    _var(iter_name), "next", std::move(next_args)))));
                            stmt->update_block = std::move(upd);
                            stmt->body = parseBlock();
                        }
                    }
                    _consumeLabel();
                    _popScope();
                    return stmt;
                }

                errorHere("for-iterator: unsupported iterable shape");
            }

            // Long form: for ( init ) ( cond ) { update } { body } [:label;]
            auto stmt = make<ForLongStmt>(t_start);
            // for-scope: covers init, cond, update, body. push a parser scope frame
            // so init-tuple decls are visible in cond/update/body and don't leak out.
            scope_stack_.push_back({});

            // init tuple: comma-separated slots; each is empty / `type name = expr`
            // / `name = expr` (decl-or-assign disambiguated by current scope).
            expect(TokenType::kLParen, "expected '('");
            if (peek().type != TokenType::kRParen) {
                while (true) {
                    if (peek().type == TokenType::kComma
                     || peek().type == TokenType::kRParen) {
                        // empty slot — skip; produces no statement.
                    } else if (isTypeName(peek())
                            || (isUserTypeName(peek()) && isVarDeclLookahead())) {
                        int slot_tok = pos_;
                        std::string type = parseTypeName();
                        int name_tok = pos_;
                        std::string name = expect(TokenType::kIdentifier,
                            "expected variable name").value;
                        expect(TokenType::kEquals,
                            "expected '=' in for-init slot");
                        auto init = parseExpr();
                        declareVar(name, name_tok);
                        stmt->init_stmts.push_back(
                            make<VarDeclStmt>(slot_tok, type, name, std::move(init)));
                    } else if (peek().type == TokenType::kIdentifier) {
                        int slot_tok = pos_;
                        std::string name = advance().value;
                        expect(TokenType::kEquals,
                            "expected '=' in for-init slot");
                        auto rhs = parseExpr();
                        // buildAssignFromLhs picks VarDeclStmt (inferred) when
                        // name is not yet in scope, AssignStmt when it is.
                        auto lhs = make<VarExpr>(slot_tok, name);
                        stmt->init_stmts.push_back(
                            buildAssignFromLhs(std::move(lhs), std::move(rhs),
                                               false, slot_tok));
                    } else {
                        errorHere("expected variable name in for-init tuple");
                    }
                    if (peek().type == TokenType::kComma) { advance(); continue; }
                    break;
                }
            }
            expect(TokenType::kRParen, "expected ')'");

            // condition: empty () means true.
            expect(TokenType::kLParen, "expected '(' for for-loop condition");
            if (peek().type == TokenType::kRParen)
                stmt->cond = nullptr;
            else
                stmt->cond = parseExpr();
            expect(TokenType::kRParen, "expected ')'");

            // update block, then body block.
            stmt->update_block = parseBlock();
            stmt->body = parseBlock();

            // optional :label;
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier,
                    "expected label name").value;
                expect(TokenType::kSemicolon, "expected ';'");
            } else {
                stmt->block_label = "for";
            }

            scope_stack_.pop_back();
            return stmt;
        }
        errorHere("expected '(' after 'for'");
    }

    // tuple return nested function or tuple destructure: starts with '('
    if (t.type == TokenType::kLParen) {
        // scan to matching ')' and check what follows; also count outer-level
        // commas so single-element parens fall through to the lvalue branch
        // (anon-tuples have minimum size 2).
        int depth = 1, scan = pos_ + 1, outer_commas = 0;
        while (scan < (int)tokens_.size() && depth > 0) {
            if (tokens_[scan].type == TokenType::kLParen) depth++;
            else if (tokens_[scan].type == TokenType::kRParen) depth--;
            else if (depth == 1 && tokens_[scan].type == TokenType::kComma) outer_commas++;
            scan++;
        }
        // scan is now at the token after the matching ')'
        if (scan < (int)tokens_.size()
            && tokens_[scan].type == TokenType::kIdentifier
            && scan + 1 < (int)tokens_.size()
            && tokens_[scan + 1].type == TokenType::kLParen) {
            // (type name, ...) funcName() { ... } — nested function with tuple return
            auto stmt = make<NestedFunctionDefStmt>(t_start);
            stmt->def = parseNestedFunctionDef();
            return stmt;
        } else if (outer_commas > 0
                   && scan < (int)tokens_.size()
                   && tokens_[scan].type == TokenType::kEquals) {
            // (type name, ...) = expr; — tuple destructure
            // slots may be: empty (skip), bare name (infer type), or type + name (declared).
            advance(); // consume '('
            auto td = make<TupleDestructureStmt>(t_start);
            if (peek().type != TokenType::kRParen) {
                while (true) {
                    // empty slot — next is ',' or ')' where a field should start
                    if (peek().type == TokenType::kComma || peek().type == TokenType::kRParen) {
                        td->fields.emplace_back("", "");
                    } else {
                        std::string type;
                        std::string name;
                        // bare name form: identifier followed by ',' or ')'
                        if (peek().type == TokenType::kIdentifier
                            && pos_ + 1 < (int)tokens_.size()
                            && (tokens_[pos_ + 1].type == TokenType::kComma
                                || tokens_[pos_ + 1].type == TokenType::kRParen)) {
                            name = advance().value;
                        } else {
                            type = parseTypeName();
                            name = expect(TokenType::kIdentifier, "expected variable name").value;
                        }
                        td->fields.emplace_back(type, name);
                    }
                    if (peek().type == TokenType::kComma) { advance(); continue; }
                    break;
                }
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kEquals, "expected '='");
            td->init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            return td;
        } else if (outer_commas > 0
                   && scan < (int)tokens_.size()
                   && tokens_[scan].type == TokenType::kIdentifier
                   && scan + 1 < (int)tokens_.size()
                   && (tokens_[scan + 1].type == TokenType::kEquals
                       || tokens_[scan + 1].type == TokenType::kArrowLeft
                       || tokens_[scan + 1].type == TokenType::kSemicolon)) {
            // (t1, t2, ...) name [= expr | <- expr]; — anon-tuple typed var decl
            advance(); // consume '('
            std::string type = "(";
            bool first = true;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                if (!first) type += ",";
                first = false;
                type += parseTypeName();
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            type += ")";
            int name_tok = pos_;
            std::string name = expect(TokenType::kIdentifier, "expected variable name").value;
            if (peek().type == TokenType::kSemicolon) {
                advance();
                declareVar(name, name_tok);
                return make<VarDeclStmt>(t_start, type, name, nullptr);
            }
            if (peek().type == TokenType::kArrowLeft) {
                advance();
                auto init = parseExpr();
                expect(TokenType::kSemicolon, "expected ';'");
                declareVar(name, name_tok);
                return make<VarDeclStmt>(t_start, type, name, std::move(init),
                                                      std::vector<std::unique_ptr<Expr>>{}, true);
            }
            expect(TokenType::kEquals, "expected '='");
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name, name_tok);
            return make<VarDeclStmt>(t_start, type, name, std::move(init));
        }
        // paren-led lvalue statement: (lvalue) <op> rhs;  /  (expr);
        auto lhs = parsePostfix(parsePrimary());
        return parseLvalueTail(std::move(lhs));
    }

    // nested function definition: type name(...) { ... }
    // distinguish from var decl (type name = expr) by lookahead for '(' then '{' after ')'
    if (isTypeName(t)) {
        // lookahead: type identifier ( ... ) {
        int lookahead = pos_ + 1; // pos_ is at type, +1 is identifier
        if (lookahead < (int)tokens_.size()
            && tokens_[lookahead].type == TokenType::kIdentifier) {
            int after_name = lookahead + 1;
            if (after_name < (int)tokens_.size()
                && tokens_[after_name].type == TokenType::kLParen) {
                // scan forward past params to find matching ) then check for {
                int depth = 1;
                int scan = after_name + 1;
                while (scan < (int)tokens_.size() && depth > 0) {
                    if (tokens_[scan].type == TokenType::kLParen) depth++;
                    else if (tokens_[scan].type == TokenType::kRParen) depth--;
                    scan++;
                }
                // scan now points past the ')'
                if (scan < (int)tokens_.size()
                    && tokens_[scan].type == TokenType::kLBrace) {
                    auto stmt = make<NestedFunctionDefStmt>(t_start);
                    stmt->def = parseNestedFunctionDef();
                    return stmt;
                }
            }
        }
        // not a nested function — fall through to variable declaration
        std::string type = parseTypeName();
        int vd_tok = pos_;
        std::string name = expect(TokenType::kIdentifier, "expected variable name").value;
        rejectReserved(file_id_, vd_tok, name, "local variable");

        // array declaration: Type name[d0][d1...] = ((..),(..),..);
        if (peek().type == TokenType::kLBracket) {
            auto arr = make<ArrayDeclStmt>(t_start);
            arr->elem_type = type;
            arr->name = name;
            while (peek().type == TokenType::kLBracket) {
                advance();
                int dim = std::stoi(expect(TokenType::kIntLiteral, "expected array dimension").value);
                expect(TokenType::kRBracket, "expected ']'");
                arr->dims.push_back(dim);
            }
            // parse nested initializer lists: flatten into row-major order
            std::function<void()> parseInitList = [&]() {
                if (peek().type == TokenType::kLParen) {
                    advance();
                    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                        parseInitList();
                        if (peek().type == TokenType::kComma) advance();
                    }
                    expect(TokenType::kRParen, "expected ')'");
                } else {
                    arr->init_values.push_back(parseExpr());
                }
            };
            if (peek().type == TokenType::kEquals) {
                advance();
                parseInitList();
            }
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(arr->name, vd_tok);
            if (!arr->dims.empty()) {
                if (auto* li = findLocal(arr->name)) {
                    li->is_array = true;
                    li->array_count = arr->dims[0];
                    li->array_rank = (int)arr->dims.size();
                }
            }
            return arr;
        }

        if (peek().type == TokenType::kArrowLeft) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name, vd_tok);
            return make<VarDeclStmt>(t_start, type, name, std::move(init), std::vector<std::unique_ptr<Expr>>{}, true);
        }
        if (peek().type == TokenType::kSemicolon) {
            advance();
            declareVar(name, vd_tok);
            return make<VarDeclStmt>(t_start, type, name, nullptr);
        }
        expect(TokenType::kEquals, "expected '='");
        auto init = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        declareVar(name, vd_tok);
        return make<VarDeclStmt>(t_start, type, name, std::move(init));
    }

    // user-defined type variable declaration: counter c; or counter c(5); or piece board[8][8] = ...
    if (t.type == TokenType::kIdentifier && isVarDeclLookahead()) {
        int decl_tok = pos_;
        std::string type = parseTypeName(); // consumes Name plus any trailing ^ or []
        int name_tok = pos_;
        std::string name = expect(TokenType::kIdentifier, "expected variable name").value;
        // (P1) vexing parse: variable name must not equal the type's bare identifier.
        {
            size_t i = 0;
            while (i < type.size() && (isalnum((unsigned char)type[i]) || type[i] == '_')) i++;
            if (type.substr(0, i) == name) {
                errorAt(decl_tok, "variable '" + name + "' shadows type '" + type + "' in same declaration");
            }
        }

        // pointer or reference variable with initializer: Type[] ptr = expr  or  Type^ ref = expr
        if (peek().type == TokenType::kEquals) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name, name_tok);
            if (auto* li = findLocal(name)) li->type = type;
            return make<VarDeclStmt>(t_start, type, name, std::move(init));
        }
        if (peek().type == TokenType::kArrowLeft) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name, name_tok);
            if (auto* li = findLocal(name)) li->type = type;
            return make<VarDeclStmt>(t_start, type, name, std::move(init), std::vector<std::unique_ptr<Expr>>{}, true);
        }

        // array declaration: Type name[d0][d1] = (...)
        if (peek().type == TokenType::kLBracket) {
            auto arr = make<ArrayDeclStmt>(t_start);
            arr->elem_type = type;
            arr->name = name;
            while (peek().type == TokenType::kLBracket) {
                advance();
                int dim = std::stoi(expect(TokenType::kIntLiteral, "expected array dimension").value);
                expect(TokenType::kRBracket, "expected ']'");
                arr->dims.push_back(dim);
            }
            std::function<void()> parseInitList = [&]() {
                if (peek().type == TokenType::kLParen) {
                    advance();
                    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                        parseInitList();
                        if (peek().type == TokenType::kComma) advance();
                    }
                    expect(TokenType::kRParen, "expected ')'");
                } else {
                    arr->init_values.push_back(parseExpr());
                }
            };
            if (peek().type == TokenType::kEquals) {
                advance();
                parseInitList();
            }
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(arr->name, name_tok);
            if (!arr->dims.empty()) {
                if (auto* li = findLocal(arr->name)) {
                    li->is_array = true;
                    li->array_count = arr->dims[0];
                    li->array_rank = (int)arr->dims.size();
                }
            }
            return arr;
        }

        std::vector<std::unique_ptr<Expr>> ctor_args;
        if (peek().type == TokenType::kLParen) {
            advance();
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                ctor_args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
        }
        std::unique_ptr<Expr> init;
        if (peek().type == TokenType::kEquals) {
            advance();
            init = parseExpr();
        }
        expect(TokenType::kSemicolon, "expected ';'");
        declareVar(name, name_tok);
        if (auto* li = findLocal(name)) li->type = type;
        return make<VarDeclStmt>(t_start, type, name, std::move(init), std::move(ctor_args));
    }

    // identifier (or self) — assignment, compound assignment, method call, or function call.
    // self routes through the same arms; the qualified-call / template-call sub-arms
    // never fire because self isn't followed by ':' or '<' in legal source.
    if (t.type == TokenType::kIdentifier || t.type == TokenType::kSelf) {
        std::string name = t.value;
        advance();

        // namespace-qualified call statement: Name:method(args);
        if (peek().type == TokenType::kColon
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIdentifier
            && pos_ + 2 < (int)tokens_.size()
            && tokens_[pos_ + 2].type == TokenType::kLParen) {
            advance(); // consume ':'
            std::string method = advance().value;
            advance(); // consume '('
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            auto call = make<CallExpr>(t_start, method, std::move(args));
            call->qualifier = name;
            return make<ExprStmt>(t_start, std::move(call));
        }

        // template call statement: name<Type,...>(args);
        if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
            advance(); // consume '<'
            std::vector<std::string> type_args;
            while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
                type_args.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kGt, "expected '>'");
            expect(TokenType::kLParen, "expected '('");
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            auto stmt = make<CallStmt>(t_start, name, std::move(args));
            stmt->type_args = std::move(type_args);
            return stmt;
        }

        // function call statement
        if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            return make<CallStmt>(t_start, name, std::move(args));
        }

        // identifier-led lvalue: parse the postfix chain and route through the
        // unified lvalue tail (handles =, <-, <->, compound op=, and bare expr).
        auto lhs = parsePostfix(make<VarExpr>(t_start, name));
        return parseLvalueTail(std::move(lhs));
    }

    errorHere("unexpected token '" + t.value + "'");
}

// --- Top-level parsing ---

NestedFunctionDef Parser::parseNestedFunctionDef() {
    [[maybe_unused]] int t_start = pos_;
    NestedFunctionDef fn;
    if (peek().type == TokenType::kLParen) {
        advance(); // consume '('
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            std::string type = parseTypeName();
            std::string name = expect(TokenType::kIdentifier, "expected field name").value;
            fn.tuple_return_fields.emplace_back(type, name);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')'");
    } else {
        fn.return_type = parseTypeName();
    }
    int fn_tok = pos_;
    fn.name = expect(TokenType::kIdentifier, "expected function name").value;
    // (P2) nested function name cannot shadow a field of the enclosing class.
    if (current_slid_fields_.count(fn.name)) {
        errorAt(fn_tok, "nested function '" + fn.name + "' shadows field of enclosing class");
    }
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        bool is_mutable = false;
        int mut_tok = pos_;
        if (peek().type == TokenType::kMutable) {
            advance();
            is_mutable = true;
        }
        std::string type = parseTypeName();
        if (is_mutable && !isParamIndirectType(type))
            errorAt(mut_tok, "'mutable' applies only to pointer types '^' and '[]'");
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        fn.params.emplace_back(type, name);
        fn.param_mutable.push_back(is_mutable);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    std::vector<std::string> param_names;
    for (auto& p : fn.params) param_names.push_back(p.second);
    fn.body = parseBlock(param_names);
    return fn;
}

MethodDef Parser::parseMethodDef() {
    [[maybe_unused]] int t_start = pos_;
    MethodDef m;
    if (peek().type == TokenType::kVirtual) {
        m.is_virtual = true;
        advance();
    }
    // new syntax: op overloads have no explicit return type (implied void / self)
    if (peek().type == TokenType::kOp) {
        m.return_type = "void";
    } else {
        m.return_type = parseTypeName();
    }
    int op_tok = pos_;
    if (peek().type == TokenType::kOp) {
        advance();
        if (auto sym = consumeOpSymbol()) m.name = "op" + *sym;
        else errorAt(op_tok, "expected operator symbol after 'op'");
    } else {
        m.name = expect(TokenType::kIdentifier, "expected method name").value;
    }
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        bool is_mutable = false;
        int mut_tok = pos_;
        if (peek().type == TokenType::kMutable) {
            advance();
            is_mutable = true;
        }
        std::string type = parseTypeName();
        if (is_mutable && !isParamIndirectType(type))
            errorAt(mut_tok, "'mutable' applies only to pointer types '^' and '[]'");
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        m.params.emplace_back(type, name);
        m.param_mutable.push_back(is_mutable);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    checkOpArity(m.name, (int)m.params.size(), op_tok);
    checkOpMutable(m.name, m.params, m.param_mutable, op_tok);
    if (peek().type == TokenType::kEquals
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kDelete) {
        // = delete; — pure-virtual marker. slot exists in vtable, no body emitted.
        if (!m.is_virtual)
            errorHere("'= delete' requires the method to be declared 'virtual'");
        advance(); advance();
        expect(TokenType::kSemicolon, "expected ';' after '= delete'");
        m.is_pure = true;
    } else if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — no body
    } else {
        std::vector<std::string> param_names;
        for (auto& p : m.params) param_names.push_back(p.second);
        m.body = parseBlock(param_names);
    }
    return m;
}

SlidDef Parser::parseSlidDef() {
    [[maybe_unused]] int t_start = pos_;
    SlidDef slid;
    int name_tok = pos_;
    slid.name = peek().value;
    advance(); // consume class name

    // a closed class accepts no more tuple-form reopens (bare-block reopens are
    // allowed via parseExternalMethodBlock and bypass this check).
    if (closed_classes_.count(slid.name))
        errorAt(name_tok, "class '" + slid.name + "' is already complete; further field/declaration reopens are not permitted");

    // save outer's alias map; nested slids in this slid's body register short→canonical here
    auto saved_alias = nested_alias_;
    nested_alias_.clear();

    // template type parameters: Vector<T> or Pair<K, V>
    if (peek().type == TokenType::kLt) {
        advance(); // consume '<'
        while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
            slid.type_params.push_back(
                expect(TokenType::kIdentifier, "expected type parameter name").value);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kGt, "expected '>' after type parameters");
    }

    // parse tuple: (type field_ = default, ...)
    expect(TokenType::kLParen, "expected '('");

    // leading ellipsis?
    if (peek().type == TokenType::kEllipsis) {
        advance(); // consume ...
        if (peek().type == TokenType::kComma) advance(); // consume , before fields
        if (peek().type == TokenType::kRParen) {
            // lone (...): disambiguate by whether the class has been seen before
            // in this TU. first occurrence ⇒ trailing-only (open incomplete);
            // subsequent ⇒ leading-only (closing reopen, no new fields).
            if (seen_classes_.count(slid.name))
                slid.has_leading_ellipsis = true;
            else
                slid.has_trailing_ellipsis = true;
        } else {
            slid.has_leading_ellipsis = true;
            slid.is_transport_impl = true;  // emits __$pinit and __$sizeof for the consumer
        }
    }

    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        // trailing ellipsis?
        if (peek().type == TokenType::kEllipsis) {
            slid.has_trailing_ellipsis = true;
            advance(); // consume ...
            if (peek().type == TokenType::kComma) advance();
            break; // ... must be last
        }
        FieldDef f;
        f.type = parseTypeName();
        int field_tok = pos_;
        f.name = expect(TokenType::kIdentifier, "expected field name").value;
        f.tok = field_tok;
        rejectReserved(file_id_, field_tok, f.name, "field");
        // inline fixed-size array field: char name_[16]
        if (peek().type == TokenType::kLBracket
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIntLiteral) {
            advance(); // consume [
            std::string sz = advance().value; // consume N
            expect(TokenType::kRBracket, "expected ']'");
            f.type += "[" + sz + "]";
        }
        if (peek().type == TokenType::kEquals) {
            advance();
            f.default_val = parseExpr();
        }
        slid.fields.push_back(std::move(f));
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");

    // (P1) class header+body merge — tuple-param name cannot equal class name.
    for (auto& f : slid.fields) {
        if (f.name == slid.name) {
            errorAt(f.tok, "tuple-param '" + f.name + "' shares enclosing class name");
        }
    }
    // (P1) tuple-param names must be unique within the tuple.
    {
        std::set<std::string> seen;
        for (auto& f : slid.fields) {
            if (!seen.insert(f.name).second) {
                errorAt(f.tok, "duplicate tuple-param '" + f.name + "' in '" + slid.name + "'");
            }
        }
    }

    // record field names to prevent method-body field assignments from being mistaken for inferred declarations
    current_slid_fields_.clear();
    for (auto& f : slid.fields) current_slid_fields_.insert(f.name);
    all_slid_fields_[slid.name] = current_slid_fields_;

    // parse body: methods and optional constructor code
    expect(TokenType::kLBrace, "expected '{'");

    auto ctor_body = make<BlockStmt>(t_start);
    bool has_ctor_code = false;

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        // explicit constructor: _() { ... }  or forward decl: _();
        if (peek().type == TokenType::kIdentifier && peek().value == "_"
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            int ctor_tok = pos_;
            advance(); // consume _
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            if (slid.has_explicit_ctor_decl)
                errorAt(ctor_tok, "constructor already defined in '" + slid.name + "'");
            slid.has_explicit_ctor_decl = true; // declared — consumer must call ctor
            if (peek().type == TokenType::kSemicolon) {
                advance(); // forward declaration only
            } else {
                slid.explicit_ctor_body = parseBlock();
            }
            continue;
        }
        // explicit destructor: ~() { ... }  or forward decl: ~();  or virtual ~() { ... }
        int dtor_tok = pos_;
        bool dtor_virtual_prefix = false;
        if (peek().type == TokenType::kVirtual
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kBitNot
            && pos_ + 2 < (int)tokens_.size()
            && tokens_[pos_ + 2].type == TokenType::kLParen) {
            dtor_virtual_prefix = true;
            advance(); // consume virtual
        }
        if (peek().type == TokenType::kBitNot
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume ~
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            if (slid.has_explicit_dtor_decl)
                errorAt(dtor_tok, "destructor already defined in '" + slid.name + "'");
            slid.has_explicit_dtor_decl = true; // declared — consumer must call dtor
            if (dtor_virtual_prefix) slid.dtor_is_virtual = true;
            if (peek().type == TokenType::kSemicolon) {
                advance(); // forward declaration only
            } else {
                slid.dtor_body = parseBlock();
            }
            continue;
        }
        // method definition: starts with a type name followed by identifier followed by (
        // also handles operator methods: op=(...)  op<-(...)  op+(...)  etc.
        // a leading `virtual` is part of the method decl — peek past it without
        // moving pos_ (so isNestedSlidDecl, which runs first, isn't confused).
        int virt_off = (peek().type == TokenType::kVirtual) ? 1 : 0;
        auto isMethodDecl = [&]() {
            int base = pos_ + virt_off;
            const Token& t0 = tokens_[base];
            // op<symbol>( without explicit return type
            if (t0.type == TokenType::kOp) {
                auto sym = peekOpSymbolAt(virt_off + 1);
                if (!sym) return false;
                int op_end = base + 1 + (*sym == "[]" ? 2 : 1);
                return op_end < (int)tokens_.size() && tokens_[op_end].type == TokenType::kLParen;
            }
            // regular method: return-type name(
            // return type may include pointer/iterator suffixes: ^ or []
            if (!(isTypeName(t0) || isUserTypeName(t0))) return false;
            int name_pos = base + 1;
            while (name_pos < (int)tokens_.size()) {
                if (tokens_[name_pos].type == TokenType::kBitXor) {
                    name_pos++;
                } else if (tokens_[name_pos].type == TokenType::kLBracket
                           && name_pos + 1 < (int)tokens_.size()
                           && tokens_[name_pos + 1].type == TokenType::kRBracket) {
                    name_pos += 2;
                } else {
                    break;
                }
            }
            if (name_pos + 1 >= (int)tokens_.size()) return false;
            if (tokens_[name_pos].type != TokenType::kIdentifier
                && tokens_[name_pos].type != TokenType::kOp) return false;
            if (tokens_[name_pos + 1].type == TokenType::kLParen) return true;
            // <return-type> op<sym>( pattern
            if (tokens_[name_pos].type != TokenType::kOp) return false;
            auto sym = peekOpSymbolAt(name_pos + 1 - pos_);
            if (!sym) return false;
            int op_end = name_pos + 1 + (*sym == "[]" ? 2 : 1);
            return op_end < (int)tokens_.size() && tokens_[op_end].type == TokenType::kLParen;
        };
        // nested slid def: Identifier ( <field-list-or-empty-or-...> ) { body }
        // distinguish from method decl (return type before identifier) and from a ctor
        // statement (call/assignment, terminated by ';'). Disambiguator: matching ')'
        // followed by '{' is unambiguously a slid body — calls/assignments end in ';'.
        auto isNestedSlidDecl = [&]() {
            if (peek().type != TokenType::kIdentifier) return false;
            if (pos_ + 1 >= (int)tokens_.size()) return false;
            if (tokens_[pos_ + 1].type != TokenType::kLParen
                && tokens_[pos_ + 1].type != TokenType::kLt) return false;
            // scan past the (...) (and optional <...> template params) to find a {
            int scan = pos_ + 1;
            if (tokens_[scan].type == TokenType::kLt) {
                int depth = 1;
                scan++;
                while (scan < (int)tokens_.size() && depth > 0) {
                    if (tokens_[scan].type == TokenType::kLt) depth++;
                    else if (tokens_[scan].type == TokenType::kGt) depth--;
                    scan++;
                }
                if (scan >= (int)tokens_.size() || tokens_[scan].type != TokenType::kLParen)
                    return false;
            }
            int depth = 1;
            scan++;
            while (scan < (int)tokens_.size() && depth > 0) {
                if (tokens_[scan].type == TokenType::kLParen) depth++;
                else if (tokens_[scan].type == TokenType::kRParen) depth--;
                scan++;
            }
            return scan < (int)tokens_.size() && tokens_[scan].type == TokenType::kLBrace;
        };
        if (isNestedSlidDecl()) {
            // save outer's per-slid context across the recursive call
            auto saved_fields = current_slid_fields_;
            SlidDef inner = parseSlidDef();
            current_slid_fields_ = saved_fields;
            // register short→canonical alias so subsequent methods of this outer
            // can refer to the nested type by its bare name
            std::string canonical = slid.name + "." + inner.name;
            nested_alias_[inner.name] = canonical;
            slid.nested_slids.push_back(std::move(inner));
            continue;
        }
        if (isMethodDecl()) {
            int method_tok = pos_;
            auto method = parseMethodDef();
            // (P1) class scope merges name + body — method name cannot equal class name.
            if (method.name == slid.name) {
                errorAt(method_tok, "method '" + method.name + "' shares enclosing class name");
            }
            slid.methods.push_back(std::move(method));
        } else {
            // constructor code
            ctor_body->stmts.push_back(parseStmt());
            has_ctor_code = true;
        }
    }

    if (has_ctor_code)
        slid.ctor_body = std::move(ctor_body);

    expect(TokenType::kRBrace, "expected '}'");
    current_slid_fields_.clear();
    nested_alias_ = std::move(saved_alias);

    // register this class as seen (disambiguates lone `(...)` on next reopen)
    // and mark it closed if this reopen has no trailing `...`.
    seen_classes_.insert(slid.name);
    if (!slid.has_trailing_ellipsis) closed_classes_.insert(slid.name);

    return slid;
}

EnumDef Parser::parseEnumDef() {
    [[maybe_unused]] int t_start = pos_;
    expect(TokenType::kEnum, "expected 'enum'");
    EnumDef e;
    e.name = expect(TokenType::kIdentifier, "expected enum name").value;
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        e.values.push_back(expect(TokenType::kIdentifier, "expected enum value").value);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    return e;
}

ExternalMethodDef Parser::parseExternalMethodDef() {
    [[maybe_unused]] int t_start = pos_;
    ExternalMethodDef em;
    // optional return type (primitive keyword); absent for ctor/dtor
    if (isTypeName(peek())) em.return_type = parseTypeName();
    // TypeName:
    em.slid_name = expect(TokenType::kIdentifier, "expected class name").value;
    // set field names for this slid so assignments to fields aren't mistaken for inferred declarations
    {
        auto fit = all_slid_fields_.find(em.slid_name);
        current_slid_fields_ = (fit != all_slid_fields_.end()) ? fit->second : std::set<std::string>{};
    }
    expect(TokenType::kColon, "expected ':'");
    // method name, or _ (ctor), or ~ (dtor)
    if (peek().type == TokenType::kIdentifier && peek().value == "_") {
        em.method_name = "_"; advance();
    } else if (peek().type == TokenType::kBitNot) {
        em.method_name = "~"; advance();
    } else {
        em.method_name = expect(TokenType::kIdentifier, "expected method name").value;
    }
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        bool is_mutable = false;
        int mut_tok = pos_;
        if (peek().type == TokenType::kMutable) {
            advance();
            is_mutable = true;
        }
        std::string type = parseTypeName();
        if (is_mutable && !isParamIndirectType(type))
            errorAt(mut_tok, "'mutable' applies only to pointer types '^' and '[]'");
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        em.params.emplace_back(type, name);
        em.param_mutable.push_back(is_mutable);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — no body
    } else {
        std::vector<std::string> param_names;
        for (auto& p : em.params) param_names.push_back(p.second);
        em.body = parseBlock(param_names);
    }
    current_slid_fields_.clear();
    return em;
}

void Parser::parseExternalMethodBlock(Program& program) {
    [[maybe_unused]] int t_start = pos_;
    // TypeName { [returnType] methodName(params) { body } ... }
    std::string slid_name = expect(TokenType::kIdentifier, "expected class name").value;
    seen_classes_.insert(slid_name);
    {
        auto fit = all_slid_fields_.find(slid_name);
        current_slid_fields_ = (fit != all_slid_fields_.end()) ? fit->second : std::set<std::string>{};
    }
    expect(TokenType::kLBrace, "expected '{'");
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        ExternalMethodDef em;
        em.slid_name = slid_name;
        if (peek().type == TokenType::kVirtual) {
            em.is_virtual = true;
            advance();
        }
        // ctor: _() { ... }
        if (peek().type == TokenType::kIdentifier && peek().value == "_"
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume _
            em.method_name = "_";
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            em.body = parseBlock();
            program.external_methods.push_back(std::move(em));
            continue;
        }
        // dtor: ~() { ... }  or  virtual ~() { ... }
        if (peek().type == TokenType::kBitNot
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume ~
            em.method_name = "~";
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            em.body = parseBlock();
            program.external_methods.push_back(std::move(em));
            continue;
        }
        // new syntax: op overloads have no explicit return type
        if (peek().type == TokenType::kOp) {
            em.return_type = "void";
        } else {
            em.return_type = parseTypeName();
        }
        int em_tok = pos_;
        if (peek().type == TokenType::kOp) {
            advance();
            if (auto sym = consumeOpSymbol()) em.method_name = "op" + *sym;
            else errorAt(em_tok, "expected operator symbol after 'op'");
        } else {
            em.method_name = expect(TokenType::kIdentifier, "expected method name").value;
        }
        // (P1) reopen merges into class scope — method name cannot equal class name.
        if (em.method_name == slid_name) {
            errorAt(em_tok, "method '" + em.method_name + "' shares enclosing class name");
        }
        expect(TokenType::kLParen, "expected '('");
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            bool is_mutable = false;
            int mut_tok = pos_;
            if (peek().type == TokenType::kMutable) {
                advance();
                is_mutable = true;
            }
            std::string type = parseTypeName();
            if (is_mutable && !isParamIndirectType(type))
                errorAt(mut_tok, "'mutable' applies only to pointer types '^' and '[]'");
            std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
            em.params.emplace_back(type, name);
            em.param_mutable.push_back(is_mutable);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')'");
        checkOpArity(em.method_name, (int)em.params.size(), em_tok);
        checkOpMutable(em.method_name, em.params, em.param_mutable, em_tok);
        if (peek().type == TokenType::kEquals
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kDelete) {
            if (!em.is_virtual)
                errorHere("'= delete' requires the method to be declared 'virtual'");
            advance(); advance();
            expect(TokenType::kSemicolon, "expected ';' after '= delete'");
            em.is_pure = true;
        } else if (peek().type == TokenType::kSemicolon) {
            advance(); // forward declaration — no body
        } else {
            std::vector<std::string> param_names;
            for (auto& p : em.params) param_names.push_back(p.second);
            em.body = parseBlock(param_names);
        }
        program.external_methods.push_back(std::move(em));
    }
    expect(TokenType::kRBrace, "expected '}'");
    current_slid_fields_.clear();
}

FunctionDef Parser::parseFunctionDef() {
    [[maybe_unused]] int t_start = pos_;
    FunctionDef fn;
    if (peek().type == TokenType::kLParen) {
        advance(); // consume '('
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            std::string type = parseTypeName();
            std::string name = expect(TokenType::kIdentifier, "expected field name").value;
            fn.tuple_return_fields.emplace_back(type, name);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')'");
    } else {
        fn.return_type = parseTypeName();
    }
    {
        int fname_tok = pos_;
        if (peek().type == TokenType::kOp) {
            auto sym = peekOpSymbolAt(1);
            errorAt(fname_tok, sym
                ? "operator 'op" + *sym
                    + "' must be a method of a class; free-function operators are not allowed"
                : "'op' is reserved; free-function operators are not allowed");
        }
        std::string fname = expect(TokenType::kIdentifier, "expected function name").value;
        fn.name = fname;
        fn.user_name = fname;
    }
    // template type params: funcname<T, U, ...>
    if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
        advance(); // consume '<'
        while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
            fn.type_params.push_back(
                expect(TokenType::kIdentifier, "expected type parameter name").value);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kGt, "expected '>'");
    }
    expect(TokenType::kLParen, "expected '('");
    int params_tok = pos_;
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        bool is_mutable = false;
        int mut_tok = pos_;
        if (peek().type == TokenType::kMutable) {
            advance();
            is_mutable = true;
        }
        std::string type = parseTypeName();
        if (is_mutable && !isParamIndirectType(type))
            errorAt(mut_tok, "'mutable' applies only to pointer types '^' and '[]'");
        int p_tok = pos_;
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        rejectReserved(file_id_, p_tok, name, "parameter");
        fn.params.emplace_back(type, name);
        fn.param_mutable.push_back(is_mutable);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    // (P1) function header+body merge — parameter name cannot equal function name.
    for (auto& p : fn.params) {
        if (p.second == fn.user_name) {
            errorAt(params_tok, "parameter '" + p.second + "' shares enclosing function name");
        }
    }
    if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — body remains null
    } else {
        std::vector<std::string> param_names;
        for (auto& p : fn.params) param_names.push_back(p.second);
        fn.body = parseBlock(param_names);
    }
    return fn;
}

// helper: find a .slh by searching a list of directories, returns "" if not found
static std::string findHeader(const std::string& module,
                               const std::vector<std::string>& search_dirs) {
    for (auto& dir : search_dirs) {
        std::string p = dir.empty() ? module + ".slh" : dir + "/" + module + ".slh";
        std::ifstream test(p);
        if (test) return p;
    }
    return "";
}

Program Parser::parse() {
    [[maybe_unused]] int t_start = pos_;
    Program program;
    while (peek().type != TokenType::kEof) {
        // import declaration: search export_path first, then import_paths, then source_dir
        if (peek().type == TokenType::kImport) {
            advance(); // consume 'import'
            std::string module = expect(TokenType::kIdentifier, "expected module name after 'import'").value;
            expect(TokenType::kSemicolon, "expected ';' after import");

            std::vector<std::string> search;
            for (auto& p : import_paths_) search.push_back(p);
            if (!source_dir_.empty()) search.push_back(source_dir_);
            if (search.empty()) search.push_back("");

            std::string header_path = findHeader(module, search);
            if (header_path.empty())
                throw CompileError{-1, 0, std::string("import: cannot find '" + module + ".slh'")};

            // import-once: skip if this header has already been loaded in this compile
            if (!imported_once_->insert(header_path).second) continue;

            program.imported_headers.push_back(header_path);
            std::ifstream in(header_path);
            std::ostringstream buf; buf << in.rdbuf();
            int hdr_file_id = sm_.openFile(header_path, buf.str(), file_id_);
            Lexer hdr_lexer(sm_, hdr_file_id);
            Parser hdr_parser(sm_, hdr_file_id, hdr_lexer.tokenize(), source_dir_, import_paths_, imported_once_);
            Program hdr = hdr_parser.parse();

            // check before moving whether any functions or slids are template declarations
            bool has_templates = false;
            for (auto& fn : hdr.functions)
                if (!fn.type_params.empty()) { has_templates = true; break; }
            if (!has_templates)
                for (auto& slid : hdr.slids)
                    if (!slid.type_params.empty()) { has_templates = true; break; }

            for (auto& fn : hdr.functions)
                if (!fn.body) program.functions.push_back(std::move(fn));
            for (auto& slid : hdr.slids) {
                program.slid_modules[slid.name] = module;
                recordSlidMethods(slid);
                program.slids.push_back(std::move(slid));
            }

            // load template bodies from impl file: foo.slh -> foo.sl
            if (has_templates) {
                std::string impl_path = header_path.substr(0, header_path.size() - 4) + ".sl";
                std::ifstream impl_in(impl_path);
                if (impl_in) {
                    program.imported_headers.push_back(impl_path);
                    std::ostringstream impl_buf; impl_buf << impl_in.rdbuf();
                    int impl_file_id = sm_.openFile(impl_path, impl_buf.str(), hdr_file_id);
                    Lexer impl_lexer(sm_, impl_file_id);
                    auto impl_cache = std::make_shared<std::set<std::string>>();
                    impl_cache->insert(header_path);
                    impl_cache->insert(impl_path);
                    Parser impl_parser(sm_, impl_file_id, impl_lexer.tokenize(), source_dir_, import_paths_, impl_cache);
                    Program impl_prog = impl_parser.parse();
                    for (size_t i = 0; i < impl_prog.functions.size(); i++) {
                        auto& fn = impl_prog.functions[i];
                        if (!fn.type_params.empty() && fn.body) {
                            fn.is_local = false;
                            fn.impl_module = module;
                            program.functions.push_back(std::move(fn));
                        }
                    }
                    for (size_t i = 0; i < impl_prog.slids.size(); i++) {
                        auto& impl_slid = impl_prog.slids[i];
                        if (impl_slid.type_params.empty()) continue;
                        impl_slid.is_local = false;
                        impl_slid.impl_module = module;
                        bool replaced = false;
                        for (auto& prog_slid : program.slids) {
                            if (prog_slid.name == impl_slid.name && !prog_slid.type_params.empty()) {
                                recordSlidMethods(impl_slid);
                                prog_slid = std::move(impl_slid);
                                replaced = true;
                                break;
                            }
                        }
                        if (!replaced) {
                            recordSlidMethods(impl_slid);
                            program.slids.push_back(std::move(impl_slid));
                        }
                    }
                }
            }
        }
        // enum definition
        else if (peek().type == TokenType::kEnum) {
            EnumDef e = parseEnumDef();
            enum_sizes_[e.name] = (int)e.values.size();
            program.enums.push_back(std::move(e));
        }
        // explicit template instantiation: Name<Types>(ParamTypes);
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLt
            && isInstantiationLookahead()) {
            std::string name = advance().value;
            advance(); // '<'
            std::vector<std::string> type_args;
            type_args.push_back(parseTypeName());
            while (peek().type == TokenType::kComma) { advance(); type_args.push_back(parseTypeName()); }
            expect(TokenType::kGt, "expected '>'");
            std::vector<std::string> param_types;
            expect(TokenType::kLParen, "expected '(' after instantiate type args");
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                param_types.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            program.instantiations.push_back({name, std::move(type_args), std::move(param_types)});
        }
        // slid class definition: bare identifier immediately followed by ( or <
        // (no return type prefix — that's what makes it a class, not a function)
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kLParen
                || tokens_[pos_ + 1].type == TokenType::kLt)) {
            SlidDef slid = parseSlidDef();
            recordSlidMethods(slid);
            program.slids.push_back(std::move(slid));
        }
        // derived class definition or forward decl: Base : Derived(...)  or  Base : Derived;
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 3 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kColon
            && tokens_[pos_ + 2].type == TokenType::kIdentifier
            && (tokens_[pos_ + 3].type == TokenType::kLParen
                || tokens_[pos_ + 3].type == TokenType::kSemicolon)) {
            std::string base_name = advance().value; // consume base name
            advance();                                // consume ':'
            if (tokens_[pos_ + 1].type == TokenType::kSemicolon) {
                SlidDef fwd;
                fwd.name = advance().value;
                fwd.base_name = base_name;
                advance(); // consume ';'
                recordSlidMethods(fwd);
                program.slids.push_back(std::move(fwd));
            } else {
                SlidDef slid = parseSlidDef();
                slid.base_name = base_name;
                recordSlidMethods(slid);
                program.slids.push_back(std::move(slid));
            }
        }
        // block-style external methods: TypeName { void method() { ... } ... }
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLBrace) {
            parseExternalMethodBlock(program);
        } else if (peek().type == TokenType::kLParen) {
            program.functions.push_back(parseFunctionDef());
        } else {
            // detect external method: [returnType] TypeName:
            // advance past return type (primitive or user type) when followed by another identifier
            int p = pos_;
            if ((isTypeName(tokens_[p]) || tokens_[p].type == TokenType::kIdentifier)
                && p + 1 < (int)tokens_.size()
                && tokens_[p + 1].type == TokenType::kIdentifier) p++;
            if (p + 1 < (int)tokens_.size()
                && tokens_[p].type == TokenType::kIdentifier
                && tokens_[p + 1].type == TokenType::kColon) {
                program.external_methods.push_back(parseExternalMethodDef());
            } else {
                program.functions.push_back(parseFunctionDef());
            }
        }
    }

    // hoist nested slid defs to top level. each nested slid is renamed
    // <Outer>.<Inner> so it's globally addressable. parent_name is taken by
    // value because push_back below may reallocate program.slids, which would
    // invalidate any reference into it.
    std::function<void(SlidDef&, std::string)> hoist =
        [&](SlidDef& s, std::string parent_name) {
            std::vector<SlidDef> nested = std::move(s.nested_slids);
            s.nested_slids.clear();
            for (auto& n : nested) {
                std::string canonical = parent_name + "." + n.name;
                n.name = canonical;
                hoist(n, canonical);
                recordSlidMethods(n);
                program.slids.push_back(std::move(n));
            }
        };
    int top_count = (int)program.slids.size();
    for (int i = 0; i < top_count; i++) {
        hoist(program.slids[i], program.slids[i].name);
    }

    // collapse multiple reopens of the same class into one merged SlidDef.
    mergeReopens(program);

    // (P1) bare-enum values inject into the enclosing (file) scope. Any
    // file-scope identifier (function name, slid name, enum type, or sibling
    // enum value) that equals an enum value is a same-scope collision.
    {
        std::map<std::string, std::string> enum_values; // value -> owning enum
        for (auto& e : program.enums) {
            for (auto& v : e.values) {
                auto ins = enum_values.emplace(v, e.name);
                if (!ins.second) {
                    throw CompileError{-1, 0, std::string("enum value '" + v + "' appears in '"
                        + ins.first->second + "' and '" + e.name + "'")};
                }
            }
        }
        for (auto& fn : program.functions) {
            auto it = enum_values.find(fn.user_name);
            if (it != enum_values.end()) {
                throw CompileError{-1, 0, std::string("function '" + fn.user_name
                    + "' collides with enum value from '" + it->second + "'")};
            }
        }
        for (auto& s : program.slids) {
            auto it = enum_values.find(s.name);
            if (it != enum_values.end()) {
                throw CompileError{-1, 0, std::string("class/namespace '" + s.name
                    + "' collides with enum value from '" + it->second + "'")};
            }
        }
        for (auto& e : program.enums) {
            auto it = enum_values.find(e.name);
            if (it != enum_values.end() && it->second != e.name) {
                throw CompileError{-1, 0, std::string("enum type '" + e.name
                    + "' collides with enum value from '" + it->second + "'")};
            }
        }
    }

    // (P1) file-scope function uniqueness — at most one definition per
    // signature. Forward declarations (body == nullptr) are pair-able with a
    // matching definition and don't count as duplicates.
    {
        std::set<std::string> sigs;
        for (auto& fn : program.functions) {
            if (!fn.body) continue;
            std::string key = fn.user_name + "(";
            for (auto& p : fn.params) key += p.first + ",";
            key += ")";
            if (!sigs.insert(key).second) {
                throw CompileError{-1, 0, std::string("function '" + fn.user_name
                    + "' redefined with same signature")};
            }
        }
    }

    // synthesize SlidDef entries for namespaces — slid names that appear only
    // in external_methods (block reopens or `void Name:fn()` defs) with no `Name(...)` data block.
    {
        std::set<std::string> existing;
        for (auto& s : program.slids) existing.insert(s.name);
        std::set<std::string> seen_ns;
        for (auto& em : program.external_methods) {
            if (existing.count(em.slid_name)) continue;
            if (!seen_ns.insert(em.slid_name).second) continue;
            SlidDef ns;
            ns.name = em.slid_name;
            ns.is_namespace = true;
            program.slids.push_back(std::move(ns));
        }
    }

    return program;
}

void Parser::mergeReopens(Program& program) {
    [[maybe_unused]] int t_start = pos_;
    // group SlidDef indices by class name in source order. skip namespaces
    // (synthesized later) and template slids (have separate machinery).
    std::map<std::string, std::vector<int>> groups;
    for (int i = 0; i < (int)program.slids.size(); i++) {
        if (program.slids[i].is_namespace) continue;
        if (!program.slids[i].type_params.empty()) continue;
        groups[program.slids[i].name].push_back(i);
    }
    std::set<int> to_remove;
    for (auto& [name, indices] : groups) {
        if (indices.size() <= 1) continue;
        SlidDef& dst = program.slids[indices[0]];
        // public_field_count: the field count of the first reopen that lacks a
        // leading `...` (the public prefix). if dst already qualifies, count is
        // its current field size; otherwise scan for one in the rest of the group.
        bool first_no_leading = !dst.has_leading_ellipsis;
        if (first_no_leading) dst.public_field_count = (int)dst.fields.size();
        for (int i = 1; i < (int)indices.size(); i++) {
            SlidDef& src = program.slids[indices[i]];
            if (!first_no_leading && !src.has_leading_ellipsis) {
                dst.public_field_count = (int)dst.fields.size() + (int)src.fields.size();
                first_no_leading = true;
            }
            // append fields in source order
            for (auto& f : src.fields) dst.fields.push_back(std::move(f));
            // OR — "any reopen contributed this"
            dst.has_explicit_ctor_decl = dst.has_explicit_ctor_decl || src.has_explicit_ctor_decl;
            dst.has_explicit_dtor_decl = dst.has_explicit_dtor_decl || src.has_explicit_dtor_decl;
            dst.is_transport_impl      = dst.is_transport_impl      || src.is_transport_impl;
            dst.has_leading_ellipsis   = dst.has_leading_ellipsis   || src.has_leading_ellipsis;
            // AND — "still open" only if every reopen left it open
            dst.has_trailing_ellipsis  = dst.has_trailing_ellipsis  && src.has_trailing_ellipsis;
            // pick the non-empty / non-null contribution
            if (dst.base_name.empty()) dst.base_name = src.base_name;
            if (!dst.ctor_body)          dst.ctor_body          = std::move(src.ctor_body);
            if (!dst.explicit_ctor_body) dst.explicit_ctor_body = std::move(src.explicit_ctor_body);
            if (!dst.dtor_body)          dst.dtor_body          = std::move(src.dtor_body);
            if (dst.impl_module.empty()) dst.impl_module = src.impl_module;
            // accumulate methods (declarations + bodies; codegen dedupes overload signatures)
            for (auto& m : src.methods) dst.methods.push_back(std::move(m));
            // accumulate any nested slids that haven't been hoisted yet
            for (auto& n : src.nested_slids) dst.nested_slids.push_back(std::move(n));
            to_remove.insert(indices[i]);
        }
    }
    if (!to_remove.empty()) {
        std::vector<SlidDef> kept;
        kept.reserve(program.slids.size() - to_remove.size());
        for (int i = 0; i < (int)program.slids.size(); i++) {
            if (!to_remove.count(i))
                kept.push_back(std::move(program.slids[i]));
        }
        program.slids = std::move(kept);
    }
    // (P1) class scope merges across reopens — at most one definition per
    // (class, method, signature). Forward declarations (body == nullptr)
    // pair with a matching definition and don't count as duplicates.
    {
        std::map<std::string, std::set<std::string>> sigs_by_class;
        auto check_dup = [&](const std::string& cls, const std::string& mname,
                              const std::vector<std::pair<std::string, std::string>>& params) {
            std::string key = mname + "(";
            for (auto& p : params) key += p.first + ",";
            key += ")";
            auto& set = sigs_by_class[cls];
            if (!set.insert(key).second) {
                throw CompileError{-1, 0, std::string("class '" + cls
                    + "': duplicate method '" + mname + "' with same signature")};
            }
        };
        for (auto& s : program.slids) {
            for (auto& m : s.methods) {
                if (!m.body) continue;
                check_dup(s.name, m.name, m.params);
            }
        }
        for (auto& em : program.external_methods) {
            if (!em.body) continue;
            check_dup(em.slid_name, em.method_name, em.params);
        }
    }
    // Inheritance shadow checks — derived field vs base method, derived method vs base field.
    {
        std::map<std::string, SlidDef*> by_name;
        for (auto& s : program.slids) by_name[s.name] = &s;
        std::map<std::string, std::set<std::string>> method_names_by_class;
        for (auto& s : program.slids)
            for (auto& m : s.methods) method_names_by_class[s.name].insert(m.name);
        for (auto& em : program.external_methods)
            method_names_by_class[em.slid_name].insert(em.method_name);
        std::map<std::string, std::set<std::string>> field_names_by_class;
        for (auto& s : program.slids)
            for (auto& f : s.fields) field_names_by_class[s.name].insert(f.name);
        for (auto& s : program.slids) {
            if (s.base_name.empty()) continue;
            auto bit = by_name.find(s.base_name);
            if (bit == by_name.end()) continue;
            auto& base_methods = method_names_by_class[s.base_name];
            auto& base_fields  = field_names_by_class[s.base_name];
            for (auto& f : s.fields) {
                if (base_methods.count(f.name)) {
                    throw CompileError{-1, 0, std::string("derived field '" + f.name
                        + "' in '" + s.name + "' shadows method inherited from '" + s.base_name + "'")};
                }
            }
            for (auto& m : s.methods) {
                if (base_fields.count(m.name)) {
                    throw CompileError{-1, 0, std::string("derived method '" + m.name
                        + "' in '" + s.name + "' shadows field inherited from '" + s.base_name + "'")};
                }
            }
        }
    }
}

std::unique_ptr<SwitchStmt> Parser::parseSwitchStmt() {
    [[maybe_unused]] int t_start = pos_;
    expect(TokenType::kSwitch, "expected 'switch'");
    expect(TokenType::kLParen, "expected '('");
    auto stmt = make<SwitchStmt>(t_start);
    stmt->expr = parseExpr();
    expect(TokenType::kRParen, "expected ')'");
    expect(TokenType::kLBrace, "expected '{'");

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        SwitchCase sc;
        if (peek().type == TokenType::kCase) {
            advance();
            colon_terminates_expr_++;
            sc.value = parseExpr();
            colon_terminates_expr_--;
            expect(TokenType::kColon, "expected ':'");
        } else if (peek().type == TokenType::kDefault) {
            advance();
            expect(TokenType::kColon, "expected ':'");
            sc.value = nullptr; // default
        } else {
            errorHere("expected 'case' or 'default' in switch");
        }
        // parse statements until next case/default/}
        while (peek().type != TokenType::kCase
            && peek().type != TokenType::kDefault
            && peek().type != TokenType::kRBrace
            && peek().type != TokenType::kEof) {
            sc.stmts.push_back(parseStmt());
        }
        stmt->cases.push_back(std::move(sc));
    }
    expect(TokenType::kRBrace, "expected '}'");
    if (peek().type == TokenType::kColon) {
        advance();
        stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
        expect(TokenType::kSemicolon, "expected ';'");
    } else {
        stmt->block_label = "switch";
    }
    return stmt;
}
