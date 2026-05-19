#include "parser.h"
#include "lexer.h"
#include "source_map.h"
#include <stdexcept>
#include <functional>
#include <fstream>
#include <sstream>
#include <map>

// Tokens that can begin an expression. Used by parsePostfix to disambiguate
// postfix-^ / postfix-^^ from binary-^ / binary-^^: if the token after a `^`
// (or `^^`) can start an expression, leave the operator for the binary loop;
// otherwise consume it as postfix dereference.
//
// Tokens with both prefix and postfix forms (++, --) or with binary forms that
// compete with postfix-^ (+, -) are intentionally excluded: postfix wins, and
// authors parenthesize for the prefix reading. kBitXor is also excluded so
// runs of `^` (lexer rule 1) all consume as postfix derefs.
namespace {

enum class OpReturnKind { NoReturn, ReturnType };

// Classify an op symbol + arity into "no return value" vs "returns a value."
// Dual-form ops (+ - ~ !) are query-form (return-type) at arity 0, otherwise no-return.
// Op ^ at arity 0 is the dereference form (returns a reference to a contained
// object); arity 2 is binary xor (no-return self-producing).
OpReturnKind classifyOpSymbol(const std::string& sym, int arity) {
    if (sym == "==" || sym == "!=" || sym == "<" || sym == ">"
        || sym == "<=" || sym == ">=") return OpReturnKind::ReturnType;
    if (sym == "[]") return OpReturnKind::ReturnType;
    if ((sym == "+" || sym == "-" || sym == "~" || sym == "!" || sym == "^") && arity == 0)
        return OpReturnKind::ReturnType;
    return OpReturnKind::NoReturn;
}

// Enforce the method-head shape rules. Throws CompileError on violation.
// return_type_tok / const_tok may be -1 when the token isn't present.
void checkMethodHeadRules(
    int file_id,
    const std::string& name,
    int arity,
    bool has_explicit_return,
    bool is_const_method,
    int name_tok,
    int return_type_tok,
    int const_tok)
{
    if (name == "_") {
        if (has_explicit_return) {
            throw CompileError{file_id, return_type_tok,
                "Constructor cannot have an explicit return type."};
        }
        return;
    }
    if (name == "~") {
        if (has_explicit_return) {
            throw CompileError{file_id, return_type_tok,
                "Destructor cannot have an explicit return type."};
        }
        return;
    }
    if (name.size() >= 3 && name.substr(0, 2) == "op") {
        std::string sym = name.substr(2);
        OpReturnKind kind = classifyOpSymbol(sym, arity);
        if (kind == OpReturnKind::NoReturn) {
            if (has_explicit_return) {
                throw CompileError{file_id, return_type_tok,
                    "Operator '" + sym + "' has no return value and cannot have an explicit return type."};
            }
            if (is_const_method) {
                throw CompileError{file_id, const_tok,
                    "Operator '" + sym + "' has no return value and cannot be const."};
            }
        } else {
            if (!has_explicit_return) {
                throw CompileError{file_id, name_tok,
                    "Operator '" + sym + "' returns a value and must declare its return type."};
            }
        }
        return;
    }
    if (!has_explicit_return) {
        throw CompileError{file_id, name_tok,
            "Method '" + name + "' is missing its return type."};
    }
}

}  // namespace

static bool isOperandStartingToken(TokenType t) {
    switch (t) {
        case TokenType::kIdentifier:
        case TokenType::kIntLiteral:
        case TokenType::kUintLiteral:
        case TokenType::kCharLiteral:
        case TokenType::kFloatLiteral:
        case TokenType::kStringLiteral:
        case TokenType::kLParen:
        case TokenType::kNot:
        case TokenType::kBitNot:
        case TokenType::kHash:
        case TokenType::kNew:
        case TokenType::kSelf:
        case TokenType::kNullptr:
        case TokenType::kSizeof:
        case TokenType::kTrue:
        case TokenType::kFalse:
            return true;
        default:
            return false;
    }
}

Parser::Parser(SourceMap& sm, int file_id, std::vector<Token> tokens,
               std::string source_dir,
               std::vector<std::string> import_paths,
               std::shared_ptr<std::set<std::string>> imported_once)
    : sm_(sm), file_id_(file_id),
      tokens_(std::move(tokens)), pos_(0), source_dir_(std::move(source_dir)),
      import_paths_(std::move(import_paths)),
      imported_once_(imported_once ? std::move(imported_once)
                                   : std::make_shared<std::set<std::string>>()) {}

// Ensure every diagnostic ends with terminal punctuation. Messages built
// with concatenations forget periods constantly; folding the rule into the
// error helpers keeps every site sentence-shaped without per-call upkeep.
static std::string finalizeErrorMsg(std::string msg) {
    if (msg.empty()) return msg;
    char last = msg.back();
    if (last != '.' && last != '!' && last != '?') msg.push_back('.');
    return msg;
}

void Parser::errorHere(const std::string& msg) {
    [[maybe_unused]] int t_start = pos_;
    throw CompileError{file_id_, pos_, finalizeErrorMsg(msg)};
}

void Parser::errorAt(int t, const std::string& msg) {
    [[maybe_unused]] int t_start = pos_;
    throw CompileError{file_id_, t, finalizeErrorMsg(msg)};
}

int Parser::currentLine() {
    [[maybe_unused]] int t_start = pos_;
    auto& tlocs = sm_.at(file_id_).tokens;
    if (pos_ < 0 || pos_ >= (int)tlocs.size()) return 0;
    return tlocs[pos_].line;
}

void Parser::declareAlias(const std::string& name, const std::string& resolved, int name_tok) {
    auto& frame = alias_stack_.back();
    auto it = frame.find(name);
    if (it != frame.end()) {
        throw CompileError{file_id_, name_tok,
            "Alias '" + name + "' is already declared in the same scope."}
            .addNote(file_id_, it->second.tok, "First declared here.");
    }
    frame[name] = AliasInfo{resolved, name_tok};
}

std::string Parser::lookupAlias(const std::string& name) const {
    for (auto it = alias_stack_.rbegin(); it != alias_stack_.rend(); ++it) {
        auto sit = it->find(name);
        if (sit != it->end()) return sit->second.resolved;
    }
    return "";
}

std::string Parser::lookupLocalClass(const std::string& name) const {
    for (auto it = local_class_stack_.rbegin(); it != local_class_stack_.rend(); ++it) {
        auto sit = it->find(name);
        if (sit != it->end()) return sit->second;
    }
    return "";
}

std::string Parser::lookupNestedFunc(const std::string& name) const {
    for (auto it = nested_func_stack_.rbegin(); it != nested_func_stack_.rend(); ++it) {
        auto sit = it->find(name);
        if (sit != it->end()) return sit->second;
    }
    return "";
}

void Parser::parseAliasDecl() {
    advance(); // 'alias'
    int name_tok = pos_;
    std::string name = expect(TokenType::kIdentifier, "Expected alias name after 'alias'").value;
    if (peek().type == TokenType::kLt) {
        errorHere("Parameterized aliases are not supported; an alias name takes no template parameters.");
    }
    expect(TokenType::kEquals, "Expected '=' after alias name");
    std::string resolved = parseTypeName();
    expect(TokenType::kSemicolon, "Expected ';' after alias declaration");
    declareAlias(name, resolved, name_tok);
}

ConstDef Parser::parseConstDef() {
    expect(TokenType::kConst, "Expected 'const'");
    ConstDef cd;
    // disambiguate `const [type] name = expr;` — type is present unless the
    // current ident is immediately followed by '='.
    bool has_type = false;
    if (isTypeName(peek())) {
        has_type = true;
    } else if (peek().type == TokenType::kLParen) {
        has_type = true; // anon-tuple type
    } else if (peek().type == TokenType::kIdentifier
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type != TokenType::kEquals) {
        has_type = true;
    }
    if (has_type) cd.declared_type = parseTypeName();
    int name_tok = pos_;
    cd.name = expect(TokenType::kIdentifier, "Expected const name").value;
    cd.file_id = file_id_;
    cd.tok = name_tok;
    expect(TokenType::kEquals, "Expected '=' after const name");
    cd.rhs = parseExpr();
    expect(TokenType::kSemicolon, "Expected ';' after const initializer");
    return cd;
}

void Parser::declareVar(const std::string& name, int name_tok) {
    // (P2) inside any method, no binding may shadow a field of the enclosing class.
    {
        auto fit = current_slid_fields_.find(name);
        if (fit != current_slid_fields_.end()) {
            throw CompileError{file_id_, name_tok,
                "Local '" + name + "' shadows a field of the enclosing class."}
                .addNote(fit->second.file_id, fit->second.tok, "Field declared here.");
        }
    }
    // (collision rule) a local must not collide with a local class declared in
    // the same block — one namespace per scope spans variables and classes.
    if (!local_class_stack_.empty() && local_class_stack_.back().count(name)) {
        throw CompileError{file_id_, name_tok,
            "Local '" + name + "' collides with a class of the same name in this scope."};
    }
    if (!scope_stack_.empty()) {
        LocalInfo info;
        info.tok = name_tok;
        auto [it, inserted] = scope_stack_.back().emplace(name, info);
        if (!inserted) {
            throw CompileError{file_id_, name_tok,
                "Local '" + name + "' is already declared in the same scope."}
                .addNote(file_id_, it->second.tok, "First declared here.");
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
    if (!s.type_params.empty()) info.type_params = s.type_params;
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
        d.reason = "Type '" + cname + "' defines "
            + std::string(has_op ? "op[] but not size." : "size but not op[].");
        return d;
    }
    auto& op_sig = ci.sigs.at("op[]");
    auto& sz_sig = ci.sigs.at("size");
    if (op_sig.param_types.size() != 1) {
        d.status = ProtocolStatus::Bad;
        d.reason = "Method op[] on type '" + cname + "' must take 1 parameter; got "
            + std::to_string(op_sig.param_types.size()) + ".";
        return d;
    }
    if (!sz_sig.param_types.empty()) {
        d.status = ProtocolStatus::Bad;
        d.reason = "Method size on type '" + cname + "' must take 0 parameters; got "
            + std::to_string(sz_sig.param_types.size()) + ".";
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
        d.reason = "Type '" + cname + "' defines some of begin/end/next but not all; missing "
            + missing + ".";
        return d;
    }
    auto& b_sig = ci.sigs.at("begin");
    auto& e_sig = ci.sigs.at("end");
    auto& n_sig = ci.sigs.at("next");
    if (b_sig.return_type != e_sig.return_type
            || b_sig.return_type != n_sig.return_type) {
        d.status = ProtocolStatus::Bad;
        d.reason = "Methods begin/end/next on type '" + cname + "' return differing types: begin returns '"
            + b_sig.return_type + "', end returns '" + e_sig.return_type
            + "', next returns '" + n_sig.return_type + "'.";
        return d;
    }
    if (!b_sig.param_types.empty()) {
        d.status = ProtocolStatus::Bad;
        d.reason = "Method begin on type '" + cname + "' must take 0 parameters; got "
            + std::to_string(b_sig.param_types.size()) + ".";
        return d;
    }
    if (!e_sig.param_types.empty()) {
        d.status = ProtocolStatus::Bad;
        d.reason = "Method end on type '" + cname + "' must take 0 parameters; got "
            + std::to_string(e_sig.param_types.size()) + ".";
        return d;
    }
    if (n_sig.param_types.size() != 1) {
        d.status = ProtocolStatus::Bad;
        d.reason = "Method next on type '" + cname + "' must take 1 parameter; got "
            + std::to_string(n_sig.param_types.size()) + ".";
        return d;
    }
    if (n_sig.param_types[0] != b_sig.return_type) {
        d.status = ProtocolStatus::Bad;
        d.reason = "Parameter type '" + n_sig.param_types[0]
            + "' of next on type '" + cname
            + "' must match the begin/end/next return type '" + b_sig.return_type + "'.";
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
        case TokenType::kFloat: case TokenType::kFloat32: case TokenType::kFloat64:
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
        errorHere(msg + ", got " + tokenLabel(peek()) + ".");
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
        case TokenType::kFloat: case TokenType::kFloat32: case TokenType::kFloat64:
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
    // optional qualified-type suffix: : Identifier (e.g. Outer:Inner), any
    // depth (Outer:Inner:Deeper:...) for arbitrarily-hoisted nested slids.
    while (i + 1 < (int)tokens_.size()
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
    // optional ^, ^^, or []
    if (i < (int)tokens_.size()
        && (tokens_[i].type == TokenType::kBitXor
         || tokens_[i].type == TokenType::kXorXor)) i++;
    else if (i + 1 < (int)tokens_.size()
             && tokens_[i].type == TokenType::kLBracket
             && tokens_[i+1].type == TokenType::kRBracket) i += 2;
    return i < (int)tokens_.size() && tokens_[i].type == TokenType::kIdentifier;
}

bool Parser::slidBodyFollows(int name_idx) const {
    // tokens_[name_idx] is the class name; `[<...>] (...) {` must follow. The
    // matching ')' followed by '{' is unambiguously a slid body — a ctor-call
    // statement ends in ';'.
    if (name_idx >= (int)tokens_.size()
        || tokens_[name_idx].type != TokenType::kIdentifier) return false;
    int scan = name_idx + 1;
    if (scan >= (int)tokens_.size()) return false;
    if (tokens_[scan].type != TokenType::kLParen
        && tokens_[scan].type != TokenType::kLt) return false;
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
}

bool Parser::isSlidDeclLookahead() const {
    // `Identifier [<...>] (...) {` — a method or nested function has a return
    // type before the name, so a lone leading identifier marks a class def.
    return tokens_[pos_].type == TokenType::kIdentifier
        && slidBodyFollows(pos_);
}

bool Parser::isDerivedSlidDeclLookahead() const {
    // `Base : Derived [<...>] (...) {`.
    return pos_ + 2 < (int)tokens_.size()
        && tokens_[pos_].type == TokenType::kIdentifier
        && tokens_[pos_ + 1].type == TokenType::kColon
        && tokens_[pos_ + 2].type == TokenType::kIdentifier
        && slidBodyFollows(pos_ + 2);
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
    if (t == TokenType::kLBracket
        && i + 1 < (int)tokens_.size()
        && tokens_[i + 1].type == TokenType::kRBracket) {
        return std::string("[]");
    }
    auto it = kOpSymbols.find(t);
    if (it == kOpSymbols.end()) return std::nullopt;
    return it->second;
}

void Parser::checkOpArity(const std::string& op_name, int actual, int op_tok,
                          const std::vector<std::unique_ptr<Expr>>& param_defaults) {
    // Allowed explicit-parameter counts per in-class op<sym>. self is implicit.
    // Unary + and - accept arity 0 (self-only), 1 (unary), 2 (binary).
    // Unary ~ and ! accept arity 0 (self-only), 1 (unary). No binary form.
    static const std::map<std::string, std::vector<int>> arity = {
        {"op=", {1}}, {"op<-", {1}}, {"op<->", {1}},
        {"op+", {0, 1, 2}}, {"op-", {0, 1, 2}},
        {"op*", {2}}, {"op/", {2}}, {"op%", {2}},
        {"op&", {2}}, {"op|", {2}}, {"op^", {0, 2}}, {"op<<", {2}}, {"op>>", {2}},
        {"op&&", {2}}, {"op||", {2}}, {"op^^", {2}},
        {"op~", {0, 1}}, {"op!", {0, 1}},
        {"op+=", {1}}, {"op-=", {1}}, {"op*=", {1}}, {"op/=", {1}}, {"op%=", {1}},
        {"op&=", {1}}, {"op|=", {1}}, {"op^=", {1}}, {"op<<=", {1}}, {"op>>=", {1}},
        {"op&&=", {1}}, {"op||=", {1}}, {"op^^=", {1}},
        {"op==", {1}}, {"op!=", {1}}, {"op<", {1}}, {"op>", {1}}, {"op<=", {1}}, {"op>=", {1}},
        {"op[]", {1}},
    };
    auto it = arity.find(op_name);
    if (it == arity.end()) return;
    // operators have fixed syntactic arity — a default could never be exercised.
    for (auto& d : param_defaults)
        if (d) errorAt(op_tok, "Operator '" + op_name
            + "' cannot have a default parameter value.");
    const auto& allowed = it->second;
    for (int n : allowed) if (n == actual) return;
    std::string list;
    for (size_t i = 0; i < allowed.size(); ++i) {
        if (i > 0) list += (i + 1 == allowed.size()) ? " or " : ", ";
        list += std::to_string(allowed[i]);
    }
    errorAt(op_tok, "Operator '" + op_name + "' requires "
        + list + " parameter" + (allowed.size() == 1 && allowed[0] == 1 ? "" : "s")
        + "; got " + std::to_string(actual) + ".");
}

void Parser::checkOpMutable(const std::string& op_name,
                            const std::vector<std::pair<std::string,std::string>>& params,
                            const std::vector<bool>& param_mutable,
                            const std::vector<int>& param_mut_toks,
                            int op_tok) {
    // op overloads always have the shape "op" + a non-identifier punctuation symbol.
    // Regular methods (parsed as identifiers) cannot have such a third character.
    if (op_name.size() < 3 || op_name.compare(0, 2, "op") != 0) return;
    char c = op_name[2];
    if (isalnum((unsigned char)c) || c == '_') return;
    bool is_exempt = (op_name == "op<-" || op_name == "op<->");
    for (size_t i = 0; i < params.size(); ++i) {
        if (is_exempt) {
            if (isParamIndirectType(params[i].first) && !param_mutable[i]) {
                errorAt(op_tok, "Pointer parameter '" + params[i].second
                    + "' of " + op_name + " requires 'mutable'.");
            }
        } else {
            if (param_mutable[i]) {
                errorAt(param_mut_toks[i],
                    "Overloaded operator parameter cannot be declared 'mutable'.");
            }
        }
    }
}

std::optional<std::string> Parser::consumeOpSymbol() {
    if (pos_ >= (int)tokens_.size()) return std::nullopt;
    TokenType t = tokens_[pos_].type;
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
               || ti.type == TokenType::kXorXor
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
               || ti.type == TokenType::kXorXor
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
               || ti.type == TokenType::kXorXor
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
    // leading qualifier: const T  or  mutable T. carried as a prefix on the
    // returned type string; binds to the entire trailing type form.
    if (peek().type == TokenType::kConst || peek().type == TokenType::kMutable) {
        std::string q = (peek().type == TokenType::kConst) ? "const " : "mutable ";
        advance();
        std::string inner = parseTypeName();
        return q + inner;
    }
    // anon-tuple type: (t1, t2, ...) — may carry trailing ^ or []
    // also paren-qualified type: (const T) or (mutable T) — the qualifier
    // binds tightly to the inner type, then the trailing ^/[] applies to
    // the qualified type. distinguished from anon-tuple by the leading
    // const/mutable token after the open paren.
    if (peek().type == TokenType::kLParen) {
        if (pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kConst
                || tokens_[pos_ + 1].type == TokenType::kMutable)) {
            advance(); // consume '('
            std::string inner = parseTypeName();
            expect(TokenType::kRParen, "Expected ')' after qualified type");
            base = "(" + inner + ")";
            while (true) {
                if (peek().type == TokenType::kBitXor) { advance(); base += "^"; }
                else if (peek().type == TokenType::kXorXor) { advance(); base += "^^"; }
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
        advance(); // consume '('
        base = "(";
        bool first = true;
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            if (!first) base += ",";
            first = false;
            base += parseTypeName();
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "Expected ')' in anon-tuple type");
        base += ")";
        // fall through to the trailing ^/[] loop below
        while (true) {
            if (peek().type == TokenType::kBitXor) { advance(); base += "^"; }
            else if (peek().type == TokenType::kXorXor) { advance(); base += "^^"; }
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
    bool resolved_alias = false;
    if (isTypeName(peek())) base = advance().value;
    else if (isUserTypeName(peek())) {
        base = advance().value;
        std::string a = lookupAlias(base);
        if (!a.empty()) {
            base = a;
            resolved_alias = true;
        } else {
            // local class in scope: substitute the base to its canonical name
            // but leave resolved_alias false — `:Inner` suffixes still apply,
            // so `InFunc:Hoisted` resolves to `<func>.<n>.InFunc.Hoisted`.
            std::string lc = lookupLocalClass(base);
            if (!lc.empty()) base = lc;
        }
    }
    else errorHere("Expected type name, got " + tokenLabel(peek()) + ".");
    // alias substitution returns a finalized type string — skip qualifier/nested-alias/template-arg
    // logic; only the trailing ^/[]/^^ loop below still applies.
    if (!resolved_alias) {
        // qualified nested-type suffix: Outer:Inner:Deeper... — consume each
        // level only when the token after the inner name isn't '(' (which
        // would be an external-method def, e.g. `Outer:method()`). Loops so
        // arbitrarily-hoisted nested slids resolve to their full dotted name.
        bool qualified = false;
        while (peek().type == TokenType::kColon
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIdentifier
            && pos_ + 2 < (int)tokens_.size()
            && tokens_[pos_ + 2].type != TokenType::kLParen) {
            advance(); // consume ':'
            std::string inner = advance().value;
            base += "." + inner;
            qualified = true;
        }
        if (!qualified) {
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
            expect(TokenType::kGt, "Expected '>' after template type args");
        }
    }
    // consume trailing ^ or [] for pointer/reference types — kept distinct:
    //   ^ = reference (no arithmetic allowed)
    //   [] = pointer  (arithmetic allowed: ++, --, +, -)
    while (true) {
        if (peek().type == TokenType::kBitXor) {
            advance();
            base += "^";
        } else if (peek().type == TokenType::kXorXor) {
            advance();
            base += "^^";
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
        expect(TokenType::kRParen, "Expected ')'");
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
    if (t.type == TokenType::kTrue || t.type == TokenType::kFalse) {
        bool v = (t.type == TokenType::kTrue);
        advance();
        auto lit = make<IntLiteralExpr>(t_start, v ? 1 : 0);
        lit->is_bool = true;
        return lit;
    }
    if (t.type == TokenType::kNullptr) { advance(); return make<NullptrExpr>(t_start); }
    if (t.type == TokenType::kNew) {
        advance();
        // placement new: new(addr) Type(args)
        if (peek().type == TokenType::kLParen) {
            advance();
            auto addr = parseExpr();
            expect(TokenType::kRParen, "Expected ')'");
            std::string elem_type = parseTypeName();
            std::vector<std::unique_ptr<Expr>> args;
            if (peek().type == TokenType::kLParen) {
                advance();
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "Expected ')'");
            }
            return make<PlacementNewExpr>(t_start, std::move(addr), elem_type, std::move(args));
        }
        std::string elem_type = parseTypeName();
        if (peek().type == TokenType::kLBracket) {
            advance();
            auto count = parseExpr();
            expect(TokenType::kRBracket, "Expected ']'");
            return make<NewExpr>(t_start, elem_type, std::move(count));
        } else if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "Expected ')'");
            return make<NewScalarExpr>(t_start, elem_type, std::move(args));
        } else {
            // new Type; — equivalent to new Type()
            return make<NewScalarExpr>(t_start, elem_type, std::vector<std::unique_ptr<Expr>>{});
        }
    }
    if (t.type == TokenType::kSizeof) {
        advance();
        expect(TokenType::kLParen, "Expected '(' after sizeof");
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
        expect(TokenType::kRParen, "Expected ')'");
        return se;
    }
    if (t.type == TokenType::kColonColon) {
        advance(); // consume '::'
        std::string name = expect(TokenType::kIdentifier, "Expected name after '::'").value;
        if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "Expected ')'");
            auto call = make<CallExpr>(t_start, name, std::move(args));
            call->qualifier = "::";
            return call;
        }
        // `::name` as a value: reference the field in the unnamed namespace.
        return make<VarExpr>(t_start, "::" + name);
    }
    if (t.type == TokenType::kSelf) {
        advance();
        return make<VarExpr>(t_start, t.value);
    }
    if (t.type == TokenType::kIdentifier) {
        advance();
        // namespace-qualified access: Name : Name [: Name]* — covers
        // `Slid:method(args)`, `parts:doors_`, and chains like `Box:lid:open_`.
        // Suppressed in contexts where ':' terminates the expression (e.g. case labels).
        if (colon_terminates_expr_ == 0
            && peek().type == TokenType::kColon
            && pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kIdentifier
                || tokens_[pos_ + 1].type == TokenType::kSelf
                || tokens_[pos_ + 1].type == TokenType::kOp
                || tokens_[pos_ + 1].type == TokenType::kSizeof)) {
            std::string path = t.value;
            // token of the final path segment — diagnostics about a qualified
            // name should caret the member being looked up, not the namespace.
            int last_seg_tok = t_start;
            while (peek().type == TokenType::kColon
                   && pos_ + 1 < (int)tokens_.size()
                   && tokens_[pos_ + 1].type == TokenType::kIdentifier) {
                advance(); // consume ':'
                last_seg_tok = pos_;
                path += ":" + advance().value;
            }
            // `<path>:sizeof()` — type-scoped sizeof on a (possibly nested)
            // class. `sizeof` is a keyword, so it is handled here rather than
            // in the Ident-only segment loop above.
            if (peek().type == TokenType::kColon
                && pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kSizeof) {
                advance(); // ':'
                advance(); // 'sizeof'
                expect(TokenType::kLParen, "Expected '(' after 'sizeof'");
                expect(TokenType::kRParen, "Expected ')' after 'sizeof('");
                auto call = make<CallExpr>(t_start, "sizeof",
                                           std::vector<std::unique_ptr<Expr>>{});
                call->qualifier = path;
                return call;
            }
            // `Base:self` — self viewed as the named base sub-object (an
            // lvalue at offset 0). Carried as a colon-joined VarExpr name.
            if (peek().type == TokenType::kColon
                && pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kSelf) {
                advance(); advance(); // ':' 'self'
                return make<VarExpr>(t_start, path + ":self");
            }
            // `Base:op<sym>(args)` — invoke the base's operator on self.
            if (peek().type == TokenType::kColon
                && pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kOp) {
                advance(); advance(); // ':' 'op'
                auto sym = consumeOpSymbol();
                if (!sym) errorHere("Expected an operator symbol after 'op'.");
                expect(TokenType::kLParen, "Expected '('");
                std::vector<std::unique_ptr<Expr>> args;
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "Expected ')'");
                auto call = make<CallExpr>(t_start, "op" + *sym, std::move(args));
                call->qualifier = path;
                return call;
            }
            if (peek().type == TokenType::kLParen) {
                // call form. qualifier carries the namespace prefix; callee is
                // the final segment. matches the 2-segment `Slid:method(` shape.
                size_t cut = path.rfind(':');
                std::string qualifier = path.substr(0, cut);
                std::string method = path.substr(cut + 1);
                advance(); // consume '('
                std::vector<std::unique_ptr<Expr>> args;
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "Expected ')'");
                auto call = make<CallExpr>(t_start, method, std::move(args));
                call->qualifier = qualifier;
                return call;
            }
            // namespace var access: path is the full colon-joined name.
            return make<VarExpr>(last_seg_tok, path);
        }
        // Bare identifier — if it names a block-scoped declarable (local
        // class or nested function) in scope, substitute the canonical name
        // so codegen resolves through the right registry. Mirrors the type-
        // position substitution in parseTypeName; the stack walks (and the
        // block-level pre-scan) deliver the same "any nesting depth, before
        // or after the textual definition" property file-scope decls have.
        std::string name = t.value;
        {
            std::string lc = lookupLocalClass(name);
            if (!lc.empty()) name = lc;
            else {
                std::string nf = lookupNestedFunc(name);
                if (!nf.empty()) name = nf;
            }
        }
        // template call: name<TypeArg,...>(args)
        if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
            advance(); // consume '<'
            std::vector<std::string> type_args;
            while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
                type_args.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kGt, "Expected '>'");
            expect(TokenType::kLParen, "Expected '('");
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "Expected ')'");
            auto call = make<CallExpr>(t_start, name, std::move(args));
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
            expect(TokenType::kRParen, "Expected ')'");
            return make<CallExpr>(t_start, name, std::move(args));
        }
        return make<VarExpr>(t_start, name);
    }
    // type conversion expression: (Type=expr)
    // distinguished from grouped expression by TypeName immediately followed by =
    if (t.type == TokenType::kLParen
        && pos_ + 1 < (int)tokens_.size()
        && (isTypeName(tokens_[pos_ + 1]) || isUserTypeName(tokens_[pos_ + 1]))) {
        int saved = pos_;
        advance(); // consume '('
        int type_tok = pos_;
        std::string type_name = parseTypeName();
        if (peek().type == TokenType::kEquals) {
            advance(); // consume '='
            // The conversion-expression form `(Type = expr)` mints a value. A
            // pointer/iterator target would alias (not copy) and silently
            // strip const — that is a reinterpret cast; spell it `<Type>`.
            bool indirect = (!type_name.empty() && type_name.back() == '^')
                || (type_name.size() >= 2
                    && type_name.substr(type_name.size() - 2) == "[]");
            if (indirect)
                errorAt(type_tok, "A pointer or iterator type is not a "
                    "conversion target — '(Type = expr)' mints a value. "
                    "Reinterpret a pointer with a '<Type>' cast.");
            auto operand = parseExpr();
            expect(TokenType::kRParen, "Expected ')' after type conversion");
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
            expect(TokenType::kRParen, "Expected ')'");
            return tuple;
        }
        expect(TokenType::kRParen, "Expected ')'");
        return first;
    }
    if (t.type == TokenType::kHashHash) {
        int src_tok = pos_;
        advance();
        std::string kw = expect(TokenType::kIdentifier,
            "Expected name, type, line, file, func, date, or time after ##").value;
        if (kw == "type") {
            expect(TokenType::kLParen, "Expected '(' after ##type");
            auto operand = parseExpr();
            expect(TokenType::kRParen, "Expected ')'");
            return make<StringifyExpr>(src_tok, kw, std::move(operand));
        }
        if (kw == "name") {
            // ##name reproduces its argument as a string literal — the content
            // is not parsed as an expression. Scan to the matching ')' by
            // bracket depth and concatenate the lexed token values.
            expect(TokenType::kLParen, "Expected '(' after ##name");
            std::string text;
            int depth = 1;
            while (peek().type != TokenType::kEof) {
                TokenType tt = peek().type;
                if (tt == TokenType::kLParen || tt == TokenType::kLBracket
                    || tt == TokenType::kLBrace) depth++;
                else if (tt == TokenType::kRParen || tt == TokenType::kRBracket
                         || tt == TokenType::kRBrace) {
                    depth--;
                    if (depth == 0) break;
                }
                text += advance().value;
            }
            expect(TokenType::kRParen, "Expected ')' to close ##name");
            auto se = make<StringifyExpr>(src_tok, "name", nullptr);
            se->text = std::move(text);
            return se;
        }
        if (kw == "line" || kw == "file" || kw == "func" || kw == "date" || kw == "time")
            return make<StringifyExpr>(src_tok, kw, nullptr);
        errorAt(src_tok, "Unknown ## operator '" + kw + "'.");
    }
    errorHere("Expected expression, got " + tokenLabel(t) + ".");
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
            else member = expect(TokenType::kIdentifier, "Expected field or method name").value;
            if (peek().type == TokenType::kLParen) {
                advance();
                std::vector<std::unique_ptr<Expr>> args;
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "Expected ')'");
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
                expect(TokenType::kRBracket, "Expected ']'");
                base = make<SliceExpr>(t_start, std::move(base), std::move(idx), std::move(end_expr));
            } else {
                expect(TokenType::kRBracket, "Expected ']'");
                base = make<ArrayIndexExpr>(t_start, std::move(base), std::move(idx));
            }
        } else if (peek().type == TokenType::kBitXor) {
            // postfix ^ is dereference only when NOT followed by an expression operand
            // (if followed by an operand-starter it's binary XOR, handled by parseBitXor)
            int next = pos_ + 1;
            TokenType ntt = (next < (int)tokens_.size()) ? tokens_[next].type : TokenType::kEof;
            if (isOperandStartingToken(ntt)) break; // leave ^ for parseBitXor
            advance();
            base = make<DerefExpr>(t_start, std::move(base));
        } else if (peek().type == TokenType::kXorXor) {
            // postfix ^^ is double-dereference when NOT followed by an operand-starter
            // (if followed by an operand-starter it's binary logical XOR, handled by parseExpr)
            int next = pos_ + 1;
            TokenType ntt = (next < (int)tokens_.size()) ? tokens_[next].type : TokenType::kEof;
            if (isOperandStartingToken(ntt)) break; // leave ^^ for parseExpr
            advance();
            base = make<DerefExpr>(t_start, make<DerefExpr>(t_start, std::move(base)));
        } else if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
            std::string op = (peek().type == TokenType::kPlusPlus) ? "post++" : "post--";
            advance();
            // ptr++^ now parses as the natural composition: UnaryExpr(post++)
            // built here, then the next loop iteration sees `^` and wraps as
            // DerefExpr. resolveLvalue's DerefExpr arm special-cases the
            // post-inc/dec composition to preserve OLD-load + advance semantics.
            base = make<UnaryExpr>(t_start, op, std::move(base));
        } else {
            break;
        }
    }
    return base;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    [[maybe_unused]] int t_start = pos_;
    // #x — desugar to (##type(x), ##name(x), ^x). The operand may be any
    // postfix lvalue expression (`obj.field_.arr_[1][2]`, etc.). It is parsed
    // twice — once for the ##type arm, once for the ^ arm — so the two arms
    // get independent ASTs; ##name reproduces the operand's lexed text.
    if (peek().type == TokenType::kHash) {
        int src_tok = pos_;
        advance();
        int operand_start = pos_;
        auto type_operand = parsePostfix(parsePrimary());
        int operand_end = pos_;
        std::string name_text;
        for (int i = operand_start; i < operand_end; i++)
            name_text += tokens_[i].value;
        pos_ = operand_start;
        auto addr_operand = parsePostfix(parsePrimary());
        auto tuple = make<TupleExpr>(src_tok);
        tuple->values.push_back(
            make<StringifyExpr>(src_tok, "type", std::move(type_operand)));
        auto name_se = make<StringifyExpr>(src_tok, "name", nullptr);
        name_se->text = std::move(name_text);
        tuple->values.push_back(std::move(name_se));
        tuple->values.push_back(make<AddrOfExpr>(src_tok, std::move(addr_operand)));
        return tuple;
    }
    // qualifier-only cast: <const> expr  or  <mutable> expr — any value.
    if (peek().type == TokenType::kLt
        && pos_ + 2 < (int)tokens_.size()
        && (tokens_[pos_ + 1].type == TokenType::kConst
            || tokens_[pos_ + 1].type == TokenType::kMutable)
        && tokens_[pos_ + 2].type == TokenType::kGt) {
        advance(); // consume <
        std::string q = (peek().type == TokenType::kConst) ? "const" : "mutable";
        advance(); // consume const/mutable
        advance(); // consume >
        auto operand = parseUnary();
        return make<QualifierCastExpr>(t_start, q, std::move(operand));
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
    expect(TokenType::kLBrace, "Expected '{'");
    scope_stack_.push_back({});
    alias_stack_.push_back({});
    local_class_stack_.push_back({});
    nested_func_stack_.push_back({});
    // Pre-scan this block for forward-decl-needed declarations at its top
    // level — local classes AND nested functions — and register each
    // short→canonical mapping in the matching stack's top frame before
    // statements parse. Slids has no forward declarations: file-scope
    // classes/functions work via the file-level two-pass, and block-scope
    // declarations get the same property here. After the pre-scan,
    // `lookupLocalClass` and `lookupNestedFunc` resolve uses — before or
    // after the textual definition, at any nesting depth — and uses
    // outside the declaring block fail to resolve (intentional block scope).
    prescanLocalClasses();
    for (auto& n : predeclare)
        declareVar(n, t_start);
    auto block = make<BlockStmt>(t_start);
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        if (peek().type == TokenType::kAlias) { parseAliasDecl(); continue; }
        block->stmts.push_back(parseStmt());
    }
    // (P1) nested function uniqueness within this block — same name + signature.
    {
        struct Site { int file_id; int tok; };
        std::map<std::string, Site> sigs;
        for (auto& st : block->stmts) {
            auto* nf = dynamic_cast<NestedFunctionDefStmt*>(st.get());
            if (!nf) continue;
            std::string key = nf->def.name + "(";
            for (auto& p : nf->def.params) key += p.first + ",";
            key += ")";
            auto [it, inserted] = sigs.emplace(key, Site{nf->file_id, nf->tok});
            if (!inserted) {
                throw CompileError{nf->file_id, nf->tok,
                    "Nested function '" + nf->def.name
                        + "' is redefined with the same signature in the same block."}
                    .addNote(it->second.file_id, it->second.tok, "First defined here.");
            }
        }
    }
    scope_stack_.pop_back();
    alias_stack_.pop_back();
    local_class_stack_.pop_back();
    nested_func_stack_.pop_back();
    expect(TokenType::kRBrace, "Expected '}'");
    return block;
}

std::unique_ptr<Stmt> Parser::buildAssignFromLhs(
        std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs, bool is_move, int op_tok) {
    int t_start = lhs->tok;

    if (auto* ve = dynamic_cast<VarExpr*>(lhs.get())) {
        std::string name = ve->name;
        // namespace-qualified names (`Ns:field`, `Ns:Sub:field`, `::field`)
        // resolve through the global registry, never to a new local.
        bool is_namespace_name = name.find(':') != std::string::npos;
        // `name != "self"` carve-out: `self = expr;` parses as
        // VarExpr(name="self") on the LHS (kSelf primary arm produces a
        // VarExpr). Without this clause, the path below would treat self as
        // an inferred-type new local declaration. self is implicit, never a
        // declared local — fall through to AssignStmt instead.
        if (!is_namespace_name
            && !isInScope(name) && !current_slid_fields_.count(name) && name != "self") {
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
    if (auto* de = dynamic_cast<DerefExpr*>(lhs.get())) {
        return make<DerefAssignStmt>(t_start, std::move(de->operand), std::move(rhs), is_move);
    }
    if (auto* fa = dynamic_cast<FieldAccessExpr*>(lhs.get())) {
        return make<FieldAssignStmt>(t_start,
            std::move(fa->object), fa->field, std::move(rhs), is_move);
    }
    if (auto* ai = dynamic_cast<ArrayIndexExpr*>(lhs.get())) {
        // Inferred-elem fixed-size array decl: `name[A][B]... = expr;` where
        // `name` is not yet in scope and every index is an integer literal.
        // Treat as ArrayDeclStmt with empty elem_type; codegen infers from
        // init_values[0] and enforces homogeneity.
        if (!is_move) {
            const Expr* cur = ai;
            std::vector<int> dims_outer_first;
            bool all_intlit = true;
            while (auto* a = dynamic_cast<const ArrayIndexExpr*>(cur)) {
                auto* lit = dynamic_cast<const IntLiteralExpr*>(a->index.get());
                if (!lit) { all_intlit = false; break; }
                dims_outer_first.push_back((int)lit->value);
                cur = a->base.get();
            }
            auto* ve = dynamic_cast<const VarExpr*>(cur);
            if (all_intlit && ve && !isInScope(ve->name)
                    && !current_slid_fields_.count(ve->name)) {
                auto arr = make<ArrayDeclStmt>(t_start);
                arr->elem_type = "";
                arr->name = ve->name;
                arr->dims.assign(dims_outer_first.rbegin(),
                                 dims_outer_first.rend());
                if (auto* te = dynamic_cast<TupleExpr*>(rhs.get())) {
                    for (auto& v : te->values)
                        arr->init_values.push_back(std::move(v));
                } else {
                    arr->init_values.push_back(std::move(rhs));
                }
                declareVar(arr->name, ve->tok);
                if (auto* li = findLocal(arr->name)) {
                    li->is_array = true;
                    li->array_count = arr->dims[0];
                    li->array_rank = (int)arr->dims.size();
                }
                return arr;
            }
        }
        return make<IndexAssignStmt>(t_start,
            std::move(ai->base), std::move(ai->index), std::move(rhs), is_move);
    }
    errorAt(op_tok, "Invalid assignment target.");
}

std::unique_ptr<Stmt> Parser::buildSwapFromLhs(
        std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs) {
    int t_start = lhs->tok;
    return make<SwapStmt>(t_start, std::move(lhs), std::move(rhs));
}

std::unique_ptr<Stmt> Parser::buildCompoundAssignFromLhs(
        std::unique_ptr<Expr> lhs, const std::string& op,
        std::unique_ptr<Expr> rhs, int op_tok) {
    if (!dynamic_cast<VarExpr*>(lhs.get())
        && !dynamic_cast<DerefExpr*>(lhs.get())
        && !dynamic_cast<FieldAccessExpr*>(lhs.get())
        && !dynamic_cast<ArrayIndexExpr*>(lhs.get())) {
        errorAt(op_tok, "Compound assignment requires an lvalue.");
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
        expect(TokenType::kSemicolon, "Expected ';'");
        return buildCompoundAssignFromLhs(std::move(lhs), cop->second, std::move(rhs), op_tok);
    }
    if (peek().type == TokenType::kEquals) {
        int op_tok = pos_;
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "Expected ';'");
        return buildAssignFromLhs(std::move(lhs), std::move(rhs), false, op_tok);
    }
    if (peek().type == TokenType::kArrowLeft) {
        int op_tok = pos_;
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "Expected ';'");
        return buildAssignFromLhs(std::move(lhs), std::move(rhs), true, op_tok);
    }
    if (peek().type == TokenType::kArrowBoth) {
        advance();
        auto rhs = parseExpr();
        expect(TokenType::kSemicolon, "Expected ';'");
        return buildSwapFromLhs(std::move(lhs), std::move(rhs));
    }
    expect(TokenType::kSemicolon, "Expected ';'");
    if (auto* mc = dynamic_cast<MethodCallExpr*>(lhs.get())) {
        return make<MethodCallStmt>(t_start, 
            std::move(mc->object), mc->method, std::move(mc->args));
    }
    return make<ExprStmt>(t_start, std::move(lhs));
}

void Parser::prescanLocalClasses() {
    // Walk this block one statement at a time. The slid-def shapes
    // — `Ident [<...>] (...) {` and `Base : Derived [<...>] (...) {` — are
    // only valid at a statement-start position; checking elsewhere
    // misclassifies a nested function with a tuple return type
    // (`(int,int) fn() {...}`) once its return-type prefix has been stepped
    // past. So at each statement-start: try class detection; otherwise skip
    // the whole statement, stopping at its terminating `;` (paren/bracket-
    // balanced) or the closing `}` of its body block. Restore pos_ on exit.
    int save = pos_;
    auto skip_balanced = [&](TokenType open, TokenType close) {
        if (pos_ >= (int)tokens_.size() || peek().type != open) return;
        int d = 0;
        while (pos_ < (int)tokens_.size()) {
            if (peek().type == open) d++;
            else if (peek().type == close) {
                d--; advance();
                if (d == 0) return;
                continue;
            }
            advance();
        }
    };
    auto skip_statement = [&]() {
        // Advance past one statement starting at the current pos_. The
        // statement ends at a `;` at the outermost level (paren/bracket
        // depth 0), or — if a `{` opens before any such `;` — at the
        // matching `}`. Nested parens/brackets/braces don't terminate.
        int pd = 0, bd = 0;
        while (pos_ < (int)tokens_.size()) {
            TokenType tt = peek().type;
            if (tt == TokenType::kEof) return;
            if (tt == TokenType::kLParen) { pd++; advance(); continue; }
            if (tt == TokenType::kRParen) { pd--; advance(); continue; }
            if (tt == TokenType::kLBracket) { bd++; advance(); continue; }
            if (tt == TokenType::kRBracket) { bd--; advance(); continue; }
            if (pd == 0 && bd == 0) {
                if (tt == TokenType::kSemicolon) { advance(); return; }
                if (tt == TokenType::kLBrace) {
                    skip_balanced(TokenType::kLBrace, TokenType::kRBrace);
                    return;
                }
                // outer '}' belongs to the enclosing block — leave it.
                if (tt == TokenType::kRBrace) return;
            }
            advance();
        }
    };
    auto register_class = [&](const std::string& short_name, int name_tok) {
        if (local_class_stack_.back().count(short_name)
            || nested_func_stack_.back().count(short_name)) {
            errorAt(name_tok, "'" + short_name
                + "' is already declared in this scope.");
        }
        std::string func_path = current_function_name_;
        for (char& c : func_path) if (c == ':') c = '.';
        std::string canonical = (func_path.empty() ? "" : func_path + ".")
            + std::to_string(local_slid_counter_++) + "." + short_name;
        local_class_stack_.back()[short_name] = canonical;
    };
    auto register_nested_fn = [&](const std::string& short_name, int name_tok) {
        // Overloads share the short name in this block; the codegen mangle
        // differentiates by param types. Only collisions to record here are
        // with same-block local classes (single namespace).
        if (local_class_stack_.back().count(short_name)) {
            errorAt(name_tok, "'" + short_name
                + "' is already declared in this scope.");
        }
        if (nested_func_stack_.back().count(short_name)) return; // overload
        std::string func_path = current_function_name_;
        for (char& c : func_path) if (c == ':') c = '.';
        std::string canonical = (func_path.empty() ? "" : func_path + ".")
            + std::to_string(local_slid_counter_++) + "." + short_name;
        nested_func_stack_.back()[short_name] = canonical;
    };
    auto skip_template_brackets = [&]() {
        if (pos_ >= (int)tokens_.size() || peek().type != TokenType::kLt) return;
        int d = 0;
        while (pos_ < (int)tokens_.size()) {
            if (peek().type == TokenType::kLt) d++;
            else if (peek().type == TokenType::kGt) {
                d--; advance();
                if (d == 0) return;
                continue;
            }
            advance();
        }
    };
    // Try-detect a nested function def at the current pos_. Pattern:
    // `<type prefix> Identifier ( ... ) { ... }`. The prefix must be non-
    // empty (otherwise it'd be a class def — caught above) and must not
    // contain a `:` at paren-depth 0 (that'd be a namespace-qualified name
    // — not a nested fn). On match: register + advance past the body `}`.
    // On miss: restore pos_ and return false so skip_statement runs.
    auto try_detect_nested_fn = [&]() -> bool {
        int save_p = pos_;
        int paren = 0;
        bool seen_colon = false;
        while (pos_ < (int)tokens_.size()) {
            TokenType tt = peek().type;
            if (paren == 0) {
                if (tt == TokenType::kSemicolon
                    || tt == TokenType::kLBrace
                    || tt == TokenType::kRBrace
                    || tt == TokenType::kEof) { pos_ = save_p; return false; }
                if (tt == TokenType::kColon) seen_colon = true;
                if (tt == TokenType::kIdentifier
                    && pos_ + 1 < (int)tokens_.size()
                    && tokens_[pos_ + 1].type == TokenType::kLParen) {
                    int name_p = pos_;
                    std::string name = peek().value;
                    advance(); // identifier
                    advance(); // '('
                    int d = 1;
                    while (pos_ < (int)tokens_.size() && d > 0) {
                        if (peek().type == TokenType::kLParen) d++;
                        else if (peek().type == TokenType::kRParen) d--;
                        advance();
                    }
                    if (d == 0
                        && pos_ < (int)tokens_.size()
                        && peek().type == TokenType::kLBrace
                        && name_p > save_p
                        && !seen_colon) {
                        skip_balanced(TokenType::kLBrace, TokenType::kRBrace);
                        register_nested_fn(name, name_p);
                        return true;
                    }
                    pos_ = save_p;
                    return false;
                }
            }
            if (tt == TokenType::kLParen) paren++;
            else if (tt == TokenType::kRParen) paren--;
            advance();
        }
        pos_ = save_p;
        return false;
    };
    while (pos_ < (int)tokens_.size() && peek().type != TokenType::kEof) {
        if (peek().type == TokenType::kRBrace) break; // close of THIS block
        if (isSlidDeclLookahead()) {
            int name_tok = pos_;
            std::string short_name = peek().value;
            advance(); // identifier
            skip_template_brackets();
            skip_balanced(TokenType::kLParen, TokenType::kRParen);
            skip_balanced(TokenType::kLBrace, TokenType::kRBrace);
            register_class(short_name, name_tok);
            continue;
        }
        if (isDerivedSlidDeclLookahead()) {
            advance(); // Base
            advance(); // ':'
            int name_tok = pos_;
            std::string short_name = peek().value;
            advance(); // Derived
            skip_template_brackets();
            skip_balanced(TokenType::kLParen, TokenType::kRParen);
            skip_balanced(TokenType::kLBrace, TokenType::kRBrace);
            register_class(short_name, name_tok);
            continue;
        }
        if (try_detect_nested_fn()) continue;
        // Some other statement — advance past it whole.
        skip_statement();
    }
    pos_ = save;
}

void Parser::collectLocalClass(SlidDef slid, const std::string& short_name,
                               int name_tok) {
    // var-vs-class collision — a local var of this name was declared earlier
    // in the same block. (declareVar catches the reverse order against the
    // pre-scan registration.)
    if (!scope_stack_.empty()
        && scope_stack_.back().count(short_name)) {
        errorAt(name_tok, "'" + short_name
            + "' is already declared in this scope.");
    }
    // Canonical name comes from the block-level pre-scan; reuse it. Same-block
    // duplicate class names already errored there. If no pre-scan registration
    // exists (defensive — should not happen via parseBlock), generate one.
    std::string canonical;
    if (!local_class_stack_.empty()) {
        auto it = local_class_stack_.back().find(short_name);
        if (it != local_class_stack_.back().end())
            canonical = it->second;
    }
    if (canonical.empty()) {
        std::string func_path = current_function_name_;
        for (char& c : func_path) if (c == ':') c = '.';
        canonical = (func_path.empty() ? "" : func_path + ".")
            + std::to_string(local_slid_counter_++) + "." + short_name;
        if (!local_class_stack_.empty())
            local_class_stack_.back()[short_name] = canonical;
    }
    slid.name = canonical;
    // Inside a template, a local class may reference an unbound type param —
    // it can't become a concrete slid until the template is instantiated.
    // Carry it with the enclosing template (drained by parseFunctionDef /
    // parseSlidDef). Outside a template, collect it as a concrete slid now.
    if (in_template_)
        pending_local_classes_.push_back(std::move(slid));
    else
        pending_slids_.push_back(std::move(slid));
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    [[maybe_unused]] int t_start = pos_;
    Token t = peek();

    // const declaration: const [type] name = expr;
    if (t.type == TokenType::kConst) {
        auto stmt = make<ConstDeclStmt>(t_start);
        stmt->def = parseConstDef();
        // Register the name in the parser's scope so subsequent `name = expr`
        // parses as AssignStmt (rebind), not as a redeclaration with shadowing.
        // The const-write rejection then fires at the AssignStmt handler.
        declareVar(stmt->def.name, stmt->def.tok);
        return stmt;
    }

    // global-qualified call statement: ::name(args);
    if (t.type == TokenType::kColonColon) {
        auto call = parseExpr();
        expect(TokenType::kSemicolon, "Expected ';'");
        return make<ExprStmt>(t_start, std::move(call));
    }

    // `global;` lifetime statement, or function-internal short-form global decl.
    if (t.type == TokenType::kGlobal) {
        if (pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kSemicolon) {
            if (current_function_name_ != "main")
                errorAt(t_start, "Global lifetime statement is only allowed in `main`.");
            advance(); // consume 'global'
            advance(); // consume ';'
            return make<GlobalLifetimeStmt>(t_start);
        }
        if (current_function_name_.empty())
            errorAt(t_start, "Global declarations inside function bodies require an enclosing function.");
        GlobalDef g = parseGlobalDef(current_function_name_, current_function_name_);
        // Register each field name in the local scope so subsequent bare reads
        // and writes inside the function body don't try to redeclare it. The
        // codegen resolves bare names to function-internal globals after the
        // locals lookup fails.
        for (auto& f : g.fields) declareVar(f.name, f.tok);
        pending_globals_.push_back(std::move(g));
        // No statement is emitted into the function body — the global lives in
        // the program-level registry.
        return make<BlockStmt>(t_start);
    }

    // local class definition: a slid defined inside a code block. The class is
    // scoped to its block — outside the block its name is not registered, so
    // it simply doesn't resolve. It is renamed to a unique internal canonical
    // name `<funcpath>.<n>.<ClassName>` and hoisted into program.slids; any
    // nested classes flatten via the post-parse hoist pass. No runtime
    // statement is emitted.
    if (isSlidDeclLookahead()) {
        int name_tok = pos_;
        std::string short_name = peek().value;
        SlidDef slid = parseSlidDef();
        collectLocalClass(std::move(slid), short_name, name_tok);
        return make<BlockStmt>(t_start);
    }

    // derived local class: `Base : Derived(...) { ... }` inside a code block.
    // The base name resolves through the local-class scope (a local class can
    // be a base), otherwise it names a file-scope class.
    if (isDerivedSlidDeclLookahead()) {
        std::string base_name = advance().value; // consume base name
        advance();                                // consume ':'
        std::string lc = lookupLocalClass(base_name);
        if (!lc.empty()) base_name = lc;
        int name_tok = pos_;
        std::string short_name = peek().value;
        SlidDef slid = parseSlidDef(base_name);
        collectLocalClass(std::move(slid), short_name, name_tok);
        return make<BlockStmt>(t_start);
    }

    if (t.type == TokenType::kDelete) {
        advance();
        auto operand = parseExpr();
        expect(TokenType::kSemicolon, "Expected ';'");
        return make<DeleteStmt>(t_start, std::move(operand));
    }

    if (t.type == TokenType::kReturn) {
        advance();
        if (peek().type == TokenType::kSemicolon) {
            advance();
            return make<ReturnStmt>(t_start, nullptr);
        }
        auto expr = parseExpr();
        expect(TokenType::kSemicolon, "Expected ';'");
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
        expect(TokenType::kSemicolon, "Expected ';'");
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
        expect(TokenType::kSemicolon, "Expected ';'");
        return stmt;
    }

    // bare new-expression as a statement: new T(args);  new T[n];  new(addr) T(args);
    // result is discarded; constructor side-effects still run.
    if (t.type == TokenType::kNew) {
        auto e = parsePostfix(parsePrimary());
        expect(TokenType::kSemicolon, "Expected ';'");
        return make<ExprStmt>(t_start, std::move(e));
    }

    // pre-increment/decrement statement: ++x;  ++ref^;  ++(ref^);
    if (t.type == TokenType::kPlusPlus || t.type == TokenType::kMinusMinus) {
        std::string op = (t.type == TokenType::kPlusPlus) ? "pre++" : "pre--";
        advance();
        auto operand = parsePostfix(parsePrimary());
        expect(TokenType::kSemicolon, "Expected ';'");
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
        expect(TokenType::kLParen, "Expected '('");
        stmt->cond = parseExpr();
        expect(TokenType::kRParen, "Expected ')'");
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
                stmt->block_label = expect(TokenType::kIdentifier, "Expected label name").value;
            } else {
                stmt->block_label = "while";
            }
            expect(TokenType::kLParen, "Expected '('");
            stmt->cond = parseExpr();
            expect(TokenType::kRParen, "Expected ')'");
            expect(TokenType::kSemicolon, "Expected ';'");
        } else {
            expect(TokenType::kLParen, "Expected '('");
            if (peek().type == TokenType::kRParen)
                stmt->cond = make<IntLiteralExpr>(t_start, 1);  // while () == while (true)
            else
                stmt->cond = parseExpr();
            expect(TokenType::kRParen, "Expected ')'");
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "Expected label name").value;
                expect(TokenType::kSemicolon, "Expected ';'");
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
                        && (tokens_[i].type == TokenType::kBitXor
                         || tokens_[i].type == TokenType::kXorXor)) {
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
                expect(TokenType::kLParen, "Expected '('");

                // for-scope: covers the desugared init/cond/update + body.
                // Loop var, synthesized helpers, and body locals all unwind here.
                scope_stack_.push_back({});

                int t_var_tok = pos_;
                std::string for_var_type, for_var_name;
                if (peek().type == TokenType::kColon) {
                    errorHere("Expected a variable name in the for-iterator.");
                } else if (isTypeName(peek())) {
                    for_var_type = parseTypeName();
                    t_var_tok = pos_;
                    for_var_name = expect(TokenType::kIdentifier,
                        "Expected variable name").value;
                } else if (peek().type == TokenType::kIdentifier
                        && isVarDeclLookahead()) {
                    for_var_type = parseTypeName();
                    t_var_tok = pos_;
                    for_var_name = expect(TokenType::kIdentifier,
                        "Expected variable name").value;
                } else {
                    t_var_tok = pos_;
                    for_var_name = expect(TokenType::kIdentifier,
                        "Expected variable name").value;
                }
                expect(TokenType::kColon, "Expected ':' in for-iterator");
                // Untyped short-form reuses an in-scope outer local of the same
                // name: init becomes an assign rather than a fresh decl, body
                // references resolve through the outer alloca, value persists
                // after the loop. Spelled type always shadows.
                bool reuse_outer = for_var_type.empty() && isInScope(for_var_name);
                if (!reuse_outer) declareVar(for_var_name, t_var_tok);

                // Effective loop-var type for protocol disambiguation. With no
                // type in the header, a reuse-outer loop var is not "inferred":
                // its type is the reused local's tracked declared type.
                std::string disambig_type = for_var_type;
                if (disambig_type.empty() && reuse_outer)
                    if (auto* li = findLocal(for_var_name)) disambig_type = li->type;

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
                auto _loopAssign = [&](std::unique_ptr<Expr> v) -> std::unique_ptr<Stmt> {
                    return make<AssignStmt>(t_var_tok, for_var_name, std::move(v), false);
                };
                auto _loopInit = [&](std::unique_ptr<Expr> v) -> std::unique_ptr<Stmt> {
                    return reuse_outer ? _loopAssign(std::move(v)) : _loopDecl(std::move(v));
                };
                auto _assign = [&](const std::string& name,
                                    std::unique_ptr<Expr> v) -> std::unique_ptr<Stmt> {
                    return make<AssignStmt>(t_start, name, std::move(v), false);
                };
                auto _consumeLabel = [&] {
                    if (peek().type == TokenType::kColon) {
                        advance();
                        stmt->block_label = expect(TokenType::kIdentifier,
                            "Expected label name").value;
                        expect(TokenType::kSemicolon, "Expected ';'");
                    }
                };
                auto _popScope = [&] {
                    scope_stack_.pop_back();
                };
                // Ref form: loop var is pointer-typed (Class^ or Class[]).
                // Switches the desugar to alias-into-source instead of copy.
                bool ref_form = !disambig_type.empty()
                    && (disambig_type.back() == '^'
                        || (disambig_type.size() >= 2
                            && disambig_type.substr(disambig_type.size() - 2) == "[]"));
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
                        loop_var_decl = _loopInit(_addrOf(_srcAt(_intLit(0))));
                    } else if (for_var_type.empty()) {
                        // Inferred type: codegen reads elem type from src[0].
                        // Under reuse, this becomes an assign into the outer alloca.
                        loop_var_decl = _loopInit(_srcAt(_intLit(0)));
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
                    // __$end holds a class's size() (intptr) or an array's
                    // count (a literal that flexes) — intptr fits both.
                    stmt->init_stmts.push_back(_decl("intptr", end_name, std::move(count_expr)));
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
                    expect(TokenType::kRParen, "Expected ')'");

                    std::string end_name = "__$end_" + std::to_string(synthetic_counter_++);
                    std::string step_name;
                    stmt->init_stmts.push_back(_loopInit(std::move(first_expr)));
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
                    expect(TokenType::kRParen, "Expected ')'");
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
                        expect(TokenType::kRParen, "Expected ')'");
                        stmt->init_stmts.push_back(reuse_outer
                            ? _loopAssign(_intLit(0))
                            : _decl("int", for_var_name, _intLit(0)));
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

                // String literal: bind to a `(const char)[]` local once, then
                // iterate. The literal is read-only — a mutable `char[]` base
                // would strip const.
                if (auto* sl = dynamic_cast<StringLiteralExpr*>(first_expr.get())) {
                    int s_len = (int)sl->value.size();
                    expect(TokenType::kRParen, "Expected ')'");
                    std::string base_name =
                        "__$base_" + std::to_string(synthetic_counter_++);
                    stmt->init_stmts.push_back(
                        _decl("(const char)[]", base_name, std::move(first_expr)));
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
                        expect(TokenType::kRParen, "Expected ')'");
                        _finishIter(iter_name, _intLit(tup_size), /*is_iter_class=*/false);
                        _consumeLabel();
                        _popScope();
                        return stmt;
                    }
                    int arr_count = arrayCountInScope(iter_name);
                    int arr_rank = arrayRankInScope(iter_name);
                    expect(TokenType::kRParen, "Expected ')'");

                    if (arr_count > 0) {
                        if (arr_rank > 1)
                            errorAt(ve->tok, "Short-form for-loops do not support multi-dimensional fixed-size array iteration.");
                        _finishIter(iter_name, _intLit(arr_count), /*is_iter_class=*/false);
                    } else {
                        std::string src_type = typeInScope(iter_name);
                        auto mit = class_info_.find(src_type);
                        // Template-instance type (mangled "Base__Arg..."): the
                        // instance is never in class_info_ (it's minted by
                        // codegen). Fall back to the base template's recorded
                        // methods and substitute type params in return types.
                        std::map<std::string, std::string> tmpl_subst;
                        if (mit == class_info_.end()) {
                            auto us = src_type.find("__");
                            if (us != std::string::npos) {
                                auto bit = class_info_.find(src_type.substr(0, us));
                                if (bit != class_info_.end()
                                    && !bit->second.type_params.empty()) {
                                    std::vector<std::string> args;
                                    std::string rest = src_type.substr(us + 2), cur;
                                    for (size_t i = 0; i <= rest.size(); i++) {
                                        if (i == rest.size()
                                            || (rest[i] == '_' && i + 1 < rest.size()
                                                && rest[i + 1] == '_')) {
                                            if (!cur.empty()) args.push_back(cur);
                                            cur.clear();
                                            i++;
                                        } else { cur += rest[i]; }
                                    }
                                    if (args.size() == bit->second.type_params.size()) {
                                        mit = bit;
                                        for (size_t i = 0; i < args.size(); i++)
                                            tmpl_subst[bit->second.type_params[i]] = args[i];
                                    }
                                }
                            }
                        }
                        ClassInfo empty_ci;
                        const ClassInfo& ci = (mit != class_info_.end()) ? mit->second : empty_ci;
                        ProtocolDiag idx_diag = classifyByValue(ci, src_type);
                        ProtocolDiag ref_diag = classifyByRef(ci, src_type);
                        // Substitute template type params in protocol return
                        // types so they compare against a concrete loop var.
                        if (!tmpl_subst.empty()) {
                            auto subT = [&](const std::string& t) {
                                std::string b = t, suf;
                                while (true) {
                                    if (b.size() >= 2 && b.substr(b.size() - 2) == "[]") {
                                        suf = "[]" + suf; b.resize(b.size() - 2);
                                    } else if (!b.empty() && b.back() == '^') {
                                        suf = "^" + suf; b.pop_back();
                                    } else { break; }
                                }
                                auto s = tmpl_subst.find(b);
                                return (s != tmpl_subst.end() ? s->second : b) + suf;
                            };
                            if (idx_diag.status == ProtocolStatus::Good)
                                idx_diag.return_type = subT(idx_diag.return_type);
                            if (ref_diag.status == ProtocolStatus::Good)
                                ref_diag.return_type = subT(ref_diag.return_type);
                        }
                        // Pick protocol per option D + compat-as-availability.
                        // A protocol is "available" iff Good AND its return is
                        // compatible with the loop variable:
                        //   op[]/size  — by-ref loop var: exact match on op[]
                        //                return; by-value: pointee widens to it.
                        //   begin/end  — loop var matches begin's return exactly
                        //                (the loop var IS the iterator).
                        // Selection:
                        //   inferred loop var + both Good: error (cannot pick).
                        //   both available: trailing-^ picks begin/end, else op[].
                        //   exactly one available: use it.
                        //   neither available: report Bad reason or incompat.
                        bool pick_by_value = false;
                        bool pick_by_ref = false;
                        bool inferred = disambig_type.empty();
                        bool loop_var_is_ref = !inferred &&
                            (disambig_type.back() == '^'
                             || (disambig_type.size() >= 2
                                 && disambig_type.substr(disambig_type.size()-2) == "[]"));
                        auto pointeeOf = [](const std::string& t) -> std::string {
                            if (t.size() >= 2 && t.substr(t.size()-2) == "[]")
                                return t.substr(0, t.size()-2);
                            if (!t.empty() && t.back() == '^')
                                return t.substr(0, t.size()-1);
                            return "";
                        };
                        bool idx_avail = false;
                        if (idx_diag.status == ProtocolStatus::Good) {
                            if (inferred) idx_avail = true;
                            else if (loop_var_is_ref)
                                idx_avail = (disambig_type == idx_diag.return_type);
                            else {
                                std::string p = pointeeOf(idx_diag.return_type);
                                idx_avail = !p.empty() && widensTo(p, disambig_type);
                            }
                        }
                        bool ref_avail = false;
                        if (ref_diag.status == ProtocolStatus::Good) {
                            if (inferred) ref_avail = true;
                            else ref_avail = (disambig_type == ref_diag.return_type);
                        }

                        if (inferred
                                && idx_diag.status == ProtocolStatus::Good
                                && ref_diag.status == ProtocolStatus::Good) {
                            errorAt(ve->tok, "Type '" + src_type
                                + "' defines both op[]/size and begin/end/next;"
                                  " the for-iterator loop variable type must be"
                                  " written explicitly to select a protocol.");
                        }

                        if (idx_avail && ref_avail) {
                            if (loop_var_is_ref) pick_by_ref = true;
                            else                 pick_by_value = true;
                        } else if (idx_avail) {
                            pick_by_value = true;
                        } else if (ref_avail) {
                            pick_by_ref = true;
                        } else if (idx_diag.status == ProtocolStatus::Absent
                                && ref_diag.status == ProtocolStatus::Absent) {
                            errorAt(ve->tok, "Type '" + src_type
                                + "' is not iterable: it defines neither op[]/size nor begin/end/next.");
                        } else if (idx_diag.status == ProtocolStatus::Bad
                                || ref_diag.status == ProtocolStatus::Bad) {
                            std::string msg;
                            if (idx_diag.status == ProtocolStatus::Bad) msg += idx_diag.reason;
                            if (ref_diag.status == ProtocolStatus::Bad) {
                                if (!msg.empty()) msg += " ";
                                msg += ref_diag.reason;
                            }
                            errorAt(ve->tok, msg);
                        } else {
                            // Both Good but neither compatible with the loop var.
                            errorAt(ve->tok, "For-iterator loop variable type '"
                                + disambig_type + "' is not compatible with the"
                                  " iteration types of '" + src_type
                                + "' (op[] returns '" + idx_diag.return_type
                                + "', begin returns '" + ref_diag.return_type + "').");
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
                            auto loop_var_decl = _loopInit(
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

                errorHere("Unsupported iterable shape in for-iterator.");
            }

            // Long form: for ( init ) ( cond ) { update } { body } [:label;]
            auto stmt = make<ForLongStmt>(t_start);
            // for-scope: covers init, cond, update, body. push a parser scope frame
            // so init-tuple decls are visible in cond/update/body and don't leak out.
            scope_stack_.push_back({});

            // init tuple: comma-separated slots; each is empty / `type name = expr`
            // / `name = expr` (decl-or-assign disambiguated by current scope).
            expect(TokenType::kLParen, "Expected '('");
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
                            "Expected variable name").value;
                        expect(TokenType::kEquals,
                            "Expected '=' in for-init slot");
                        auto init = parseExpr();
                        declareVar(name, name_tok);
                        stmt->init_stmts.push_back(
                            make<VarDeclStmt>(slot_tok, type, name, std::move(init)));
                    } else if (peek().type == TokenType::kIdentifier) {
                        int slot_tok = pos_;
                        std::string name = advance().value;
                        expect(TokenType::kEquals,
                            "Expected '=' in for-init slot");
                        auto rhs = parseExpr();
                        // buildAssignFromLhs picks VarDeclStmt (inferred) when
                        // name is not yet in scope, AssignStmt when it is.
                        auto lhs = make<VarExpr>(slot_tok, name);
                        stmt->init_stmts.push_back(
                            buildAssignFromLhs(std::move(lhs), std::move(rhs),
                                               false, slot_tok));
                    } else {
                        errorHere("Expected a variable name in the for-init tuple.");
                    }
                    if (peek().type == TokenType::kComma) { advance(); continue; }
                    break;
                }
            }
            expect(TokenType::kRParen, "Expected ')'");

            // condition: empty () means true.
            expect(TokenType::kLParen, "Expected '(' for for-loop condition");
            if (peek().type == TokenType::kRParen)
                stmt->cond = nullptr;
            else
                stmt->cond = parseExpr();
            expect(TokenType::kRParen, "Expected ')'");

            // update block, then body block.
            stmt->update_block = parseBlock();
            stmt->body = parseBlock();

            // optional :label;
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier,
                    "Expected label name").value;
                expect(TokenType::kSemicolon, "Expected ';'");
            } else {
                stmt->block_label = "for";
            }

            scope_stack_.pop_back();
            return stmt;
        }
        errorHere("Expected '(' after 'for'.");
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
        }
        if (outer_commas > 0
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
                            name = expect(TokenType::kIdentifier, "Expected variable name").value;
                        }
                        td->fields.emplace_back(type, name);
                    }
                    if (peek().type == TokenType::kComma) { advance(); continue; }
                    break;
                }
            }
            expect(TokenType::kRParen, "Expected ')'");
            expect(TokenType::kEquals, "Expected '='");
            td->init = parseExpr();
            expect(TokenType::kSemicolon, "Expected ';'");
            return td;
        }
        // anon-tuple typed declaration: `(t1, t2, ...) name <decl-tail>` for
        // any of the four decl-tails (`;`, `=`, `<-`, `[`). Falls through to
        // the named-type decl path below — parseTypeName already consumes
        // the LParen anon-tuple form, so anon-tuple types are first-class.
        bool is_anon_tuple_decl = (outer_commas > 0
            && scan < (int)tokens_.size()
            && tokens_[scan].type == TokenType::kIdentifier
            && scan + 1 < (int)tokens_.size()
            && (tokens_[scan + 1].type == TokenType::kEquals
                || tokens_[scan + 1].type == TokenType::kArrowLeft
                || tokens_[scan + 1].type == TokenType::kSemicolon
                || tokens_[scan + 1].type == TokenType::kLBracket));
        // paren-qualified type declaration: `(const T) name ...` or
        // `(mutable T) name ...` (with optional `^`/`[]` suffix). A `(` is
        // unambiguously a type, not an expression, when `const`/`mutable`
        // follows — those are type-qualifier keywords.
        bool is_paren_qual_decl = pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kConst
                || tokens_[pos_ + 1].type == TokenType::kMutable);
        if (!is_anon_tuple_decl && !is_paren_qual_decl) {
            // paren-led lvalue statement: (lvalue) <op> rhs;  /  (expr);
            auto lhs = parsePostfix(parsePrimary());
            return parseLvalueTail(std::move(lhs));
        }
        // else: fall through to the named-type decl path below.
    }

    // typed declaration: built-in type or anon-tuple type. Anon-tuple decls
    // arrive here via the LParen-branch fall-through above. The nested-function
    // lookahead applies only to built-in types — anon-tuple-return nested
    // functions (`(int, int) foo() { ... }`) are caught earlier in the
    // LParen branch.
    if (isTypeName(t) || t.type == TokenType::kLParen) {
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
        }
        // not a nested function — fall through to variable declaration
        std::string type = parseTypeName();
        int vd_tok = pos_;
        std::string name = expect(TokenType::kIdentifier, "Expected variable name").value;
        rejectReserved(file_id_, vd_tok, name, "local variable");

        // array declaration: Type name[d0][d1...] = ((..),(..),..);
        if (peek().type == TokenType::kLBracket) {
            auto arr = make<ArrayDeclStmt>(t_start);
            arr->elem_type = type;
            arr->name = name;
            while (peek().type == TokenType::kLBracket) {
                advance();
                int dim = std::stoi(expect(TokenType::kIntLiteral, "Expected array dimension").value);
                expect(TokenType::kRBracket, "Expected ']'");
                arr->dims.push_back(dim);
            }
            // Parse the outer initializer list as one expression per declared
            // slot. Each slot expression may itself be a tuple literal — the
            // structure is preserved (no flattening) and handed to the codegen
            // desugar, which recurses per slot. Single non-paren expressions
            // (e.g. `(10, 20, 30)` for a primitive array) parse the same way:
            // each comma-separated value becomes its own init_values entry.
            auto parseInitList = [&]() {
                if (peek().type == TokenType::kLParen) {
                    advance();
                    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                        arr->init_values.push_back(parseExpr());
                        if (peek().type == TokenType::kComma) advance();
                    }
                    expect(TokenType::kRParen, "Expected ')'");
                } else {
                    // Single-expression RHS: covers tuple-variable RHS,
                    // tuple-returning call, single-value-promoted RHS. The
                    // codegen desugar interprets the structure.
                    arr->init_values.push_back(parseExpr());
                }
            };
            if (peek().type == TokenType::kEquals) {
                advance();
                parseInitList();
            }
            expect(TokenType::kSemicolon, "Expected ';'");
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
            expect(TokenType::kSemicolon, "Expected ';'");
            declareVar(name, vd_tok);
            return make<VarDeclStmt>(t_start, type, name, std::move(init), std::vector<std::unique_ptr<Expr>>{}, true);
        }
        if (peek().type == TokenType::kSemicolon) {
            advance();
            declareVar(name, vd_tok);
            return make<VarDeclStmt>(t_start, type, name, nullptr);
        }
        expect(TokenType::kEquals, "Expected '='");
        auto init = parseExpr();
        expect(TokenType::kSemicolon, "Expected ';'");
        declareVar(name, vd_tok);
        return make<VarDeclStmt>(t_start, type, name, std::move(init));
    }

    // user-defined type variable declaration: counter c; or counter c(5); or piece board[8][8] = ...
    if (t.type == TokenType::kIdentifier && isVarDeclLookahead()) {
        int decl_tok = pos_;
        std::string type = parseTypeName(); // consumes Name plus any trailing ^ or []
        int name_tok = pos_;
        std::string name = expect(TokenType::kIdentifier, "Expected variable name").value;
        // (P1) vexing parse: variable name must not equal the type's bare identifier.
        {
            size_t i = 0;
            while (i < type.size() && (isalnum((unsigned char)type[i]) || type[i] == '_')) i++;
            if (type.substr(0, i) == name) {
                errorAt(decl_tok, "Variable '" + name + "' shadows type '" + type + "' in the same declaration.");
            }
        }

        // pointer or reference variable with initializer: Type[] ptr = expr  or  Type^ ref = expr
        if (peek().type == TokenType::kEquals) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "Expected ';'");
            declareVar(name, name_tok);
            if (auto* li = findLocal(name)) li->type = type;
            return make<VarDeclStmt>(t_start, type, name, std::move(init));
        }
        if (peek().type == TokenType::kArrowLeft) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "Expected ';'");
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
                int dim = std::stoi(expect(TokenType::kIntLiteral, "Expected array dimension").value);
                expect(TokenType::kRBracket, "Expected ']'");
                arr->dims.push_back(dim);
            }
            // Parse the outer initializer list as one expression per declared
            // slot — see the built-in array-decl path above for the rationale.
            auto parseInitList = [&]() {
                if (peek().type == TokenType::kLParen) {
                    advance();
                    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                        arr->init_values.push_back(parseExpr());
                        if (peek().type == TokenType::kComma) advance();
                    }
                    expect(TokenType::kRParen, "Expected ')'");
                } else {
                    // Single-expression RHS: covers tuple-variable RHS,
                    // tuple-returning call, single-value-promoted RHS. The
                    // codegen desugar interprets the structure.
                    arr->init_values.push_back(parseExpr());
                }
            };
            if (peek().type == TokenType::kEquals) {
                advance();
                parseInitList();
            }
            expect(TokenType::kSemicolon, "Expected ';'");
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
            expect(TokenType::kRParen, "Expected ')'");
        }
        std::unique_ptr<Expr> init;
        if (peek().type == TokenType::kEquals) {
            advance();
            init = parseExpr();
        }
        expect(TokenType::kSemicolon, "Expected ';'");
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

        // namespace-qualified lead: Name : Name [: Name]* — covers
        // `Slid:method(...);`, `parts:doors_ = 4;`, and chains like
        // `Box:lid:open_ = true;`. Greedily consume the colon chain.
        if (peek().type == TokenType::kColon
            && pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kIdentifier
                || tokens_[pos_ + 1].type == TokenType::kSelf
                || tokens_[pos_ + 1].type == TokenType::kOp)) {
            std::string path = name;
            while (peek().type == TokenType::kColon
                   && pos_ + 1 < (int)tokens_.size()
                   && tokens_[pos_ + 1].type == TokenType::kIdentifier) {
                advance(); // consume ':'
                path += ":" + advance().value;
            }
            // `Base:self` — self as a base sub-object; route to the lvalue tail.
            if (peek().type == TokenType::kColon
                && pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kSelf) {
                advance(); advance(); // ':' 'self'
                auto lhs = parsePostfix(make<VarExpr>(t_start, path + ":self"));
                return parseLvalueTail(std::move(lhs));
            }
            // `Base:op<sym>(args);` — invoke the base's operator on self.
            if (peek().type == TokenType::kColon
                && pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kOp) {
                advance(); advance(); // ':' 'op'
                auto sym = consumeOpSymbol();
                if (!sym) errorHere("Expected an operator symbol after 'op'.");
                expect(TokenType::kLParen, "Expected '('");
                std::vector<std::unique_ptr<Expr>> args;
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "Expected ')'");
                expect(TokenType::kSemicolon, "Expected ';'");
                auto call = make<CallExpr>(t_start, "op" + *sym, std::move(args));
                call->qualifier = path;
                return make<ExprStmt>(t_start, std::move(call));
            }
            // Namespace call statement: <ns-path>:method(args);
            if (peek().type == TokenType::kLParen) {
                size_t cut = path.rfind(':');
                std::string qualifier = path.substr(0, cut);
                std::string method = path.substr(cut + 1);
                advance(); // consume '('
                std::vector<std::unique_ptr<Expr>> args;
                while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                    args.push_back(parseExpr());
                    if (peek().type == TokenType::kComma) advance();
                }
                expect(TokenType::kRParen, "Expected ')'");
                expect(TokenType::kSemicolon, "Expected ';'");
                auto call = make<CallExpr>(t_start, method, std::move(args));
                call->qualifier = qualifier;
                return make<ExprStmt>(t_start, std::move(call));
            }
            // Namespace lvalue/rvalue: hand off to the lvalue tail.
            auto lhs = parsePostfix(make<VarExpr>(t_start, path));
            return parseLvalueTail(std::move(lhs));
        }

        // Bare identifier — substitute a block-scoped declarable's short name
        // (local class or nested function) with its canonical name. Mirrors
        // the same lookup in parsePrimary; the block-level pre-scan makes
        // the registration visible before the textual definition.
        if (t.type == TokenType::kIdentifier) {
            std::string lc = lookupLocalClass(name);
            if (!lc.empty()) name = lc;
            else {
                std::string nf = lookupNestedFunc(name);
                if (!nf.empty()) name = nf;
            }
        }

        // template call statement: name<Type,...>(args);
        if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
            advance(); // consume '<'
            std::vector<std::string> type_args;
            while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
                type_args.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kGt, "Expected '>'");
            expect(TokenType::kLParen, "Expected '('");
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "Expected ')'");
            expect(TokenType::kSemicolon, "Expected ';'");
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
            expect(TokenType::kRParen, "Expected ')'");
            expect(TokenType::kSemicolon, "Expected ';'");
            return make<CallStmt>(t_start, name, std::move(args));
        }

        // identifier-led lvalue: parse the postfix chain and route through the
        // unified lvalue tail (handles =, <-, <->, compound op=, and bare expr).
        auto lhs = parsePostfix(make<VarExpr>(t_start, name));
        return parseLvalueTail(std::move(lhs));
    }

    errorHere("Unexpected token '" + t.value + "'.");
}

// --- Top-level parsing ---

void Parser::parseParamList(
    std::vector<std::pair<std::string, std::string>>& params,
    std::vector<bool>& param_mutable,
    std::vector<int>& param_mut_toks,
    std::vector<std::unique_ptr<Expr>>& param_defaults)
{
    bool seen_default = false;
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        bool is_mutable = false;
        int mut_tok = pos_;
        if (peek().type == TokenType::kMutable) { advance(); is_mutable = true; }
        std::string type = parseTypeName();
        if (is_mutable && !isParamIndirectType(type))
            errorAt(mut_tok, "The 'mutable' keyword applies only to pointer types '^' and '[]'.");
        int p_tok = pos_;
        std::string name = expect(TokenType::kIdentifier, "Expected parameter name").value;
        rejectReserved(file_id_, p_tok, name, "parameter");
        params.emplace_back(type, name);
        param_mutable.push_back(is_mutable);
        param_mut_toks.push_back(mut_tok);
        std::unique_ptr<Expr> dflt;
        if (peek().type == TokenType::kEquals) {
            advance();
            dflt = parseExpr();
            seen_default = true;
        } else if (seen_default) {
            errorAt(p_tok, "Parameter '" + name + "' has no default but follows a "
                "defaulted parameter; defaults must be trailing.");
        }
        param_defaults.push_back(std::move(dflt));
        if (peek().type == TokenType::kComma) advance();
    }
}

NestedFunctionDef Parser::parseNestedFunctionDef() {
    [[maybe_unused]] int t_start = pos_;
    NestedFunctionDef fn;
    // A `(` followed by `const`/`mutable` is a paren-qualified return type,
    // not a named-tuple return list.
    bool paren_qual_return = peek().type == TokenType::kLParen
        && pos_ + 1 < (int)tokens_.size()
        && (tokens_[pos_ + 1].type == TokenType::kConst
            || tokens_[pos_ + 1].type == TokenType::kMutable);
    if (peek().type == TokenType::kLParen && !paren_qual_return) {
        advance(); // consume '('
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            std::string type = parseTypeName();
            std::string name = expect(TokenType::kIdentifier, "Expected field name").value;
            fn.tuple_return_fields.emplace_back(type, name);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "Expected ')'");
    } else {
        fn.return_type = parseTypeName();
    }
    int fn_tok = pos_;
    fn.name = expect(TokenType::kIdentifier, "Expected function name").value;
    // (P2) nested function name cannot shadow a field of the enclosing class.
    {
        auto fit = current_slid_fields_.find(fn.name);
        if (fit != current_slid_fields_.end()) {
            throw CompileError{file_id_, fn_tok,
                "Nested function '" + fn.name + "' shadows a field of the enclosing class."}
                .addNote(fit->second.file_id, fit->second.tok, "Field declared here.");
        }
    }
    // The block-level pre-scan assigned a canonical name (`<funcpath>.<n>.<short>`)
    // for this nested function in the current block's nested_func_stack_ frame.
    // Rename to canonical so two same-name fns in different blocks get distinct
    // mangles, and so the codegen `findNested` walker keys overloads off the
    // block-unique name. Outside the block its short name is unresolvable.
    {
        std::string lc = lookupNestedFunc(fn.name);
        if (!lc.empty()) fn.name = lc;
    }
    expect(TokenType::kLParen, "Expected '('");
    parseParamList(fn.params, fn.param_mutable, fn.param_mut_toks, fn.param_defaults);
    expect(TokenType::kRParen, "Expected ')'");
    std::vector<std::string> param_names;
    for (auto& p : fn.params) param_names.push_back(p.second);
    // Nested function body: scope `global` declarations to the nested name.
    // Outer-function globals are already in `current_function_name_`; nest
    // by colon-qualifying so the two namespaces don't collide.
    std::string saved_fn = current_function_name_;
    current_function_name_ = current_function_name_.empty()
        ? fn.name : (current_function_name_ + ":" + fn.name);
    fn.body = parseBlock(param_names);
    current_function_name_ = saved_fn;
    return fn;
}

MethodDef Parser::parseMethodDef(const std::string& class_name) {
    [[maybe_unused]] int t_start = pos_;
    MethodDef m;
    if (peek().type == TokenType::kVirtual) {
        m.is_virtual = true;
        advance();
    }
    int return_type_tok = -1;
    int const_tok = -1;
    // Leading `const` immediately before an elided-return method-name
    // (`op<sym>`, `_`, `~`) is the method-const marker; no return type to parse.
    bool leading_const_elision = false;
    if (peek().type == TokenType::kConst && pos_ + 1 < (int)tokens_.size()) {
        const Token& after = tokens_[pos_ + 1];
        if (after.type == TokenType::kOp
            || after.type == TokenType::kBitNot
            || (after.type == TokenType::kIdentifier && after.value == "_")) {
            const_tok = pos_;
            advance();
            m.is_const_method = true;
            leading_const_elision = true;
        }
    }
    if (leading_const_elision) {
        m.return_type = "void";
        m.has_explicit_return = false;
    } else if (peek().type == TokenType::kOp
        || peek().type == TokenType::kBitNot
        || (peek().type == TokenType::kIdentifier && peek().value == "_"
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen)) {
        // elided-return method-name with no leading const.
        m.return_type = "void";
        m.has_explicit_return = false;
    } else {
        return_type_tok = pos_;
        m.return_type = parseTypeName();
        m.has_explicit_return = true;
        // optional `const` between return type and method name
        if (peek().type == TokenType::kConst) {
            const_tok = pos_;
            m.is_const_method = true;
            advance();
        }
    }
    int op_tok = pos_;
    m.file_id = file_id_;
    m.tok = op_tok;
    if (peek().type == TokenType::kOp) {
        advance();
        if (auto sym = consumeOpSymbol()) m.name = "op" + *sym;
        else errorAt(op_tok, "Expected an operator symbol after 'op'.");
    } else if (peek().type == TokenType::kBitNot) {
        advance();
        m.name = "~";
    } else {
        m.name = expect(TokenType::kIdentifier, "Expected method name").value;
    }
    expect(TokenType::kLParen, "Expected '('");
    parseParamList(m.params, m.param_mutable, m.param_mut_toks, m.param_defaults);
    expect(TokenType::kRParen, "Expected ')'");
    checkOpArity(m.name, (int)m.params.size(), op_tok, m.param_defaults);
    checkOpMutable(m.name, m.params, m.param_mutable, m.param_mut_toks, op_tok);
    checkMethodHeadRules(file_id_, m.name, (int)m.params.size(),
        m.has_explicit_return, m.is_const_method,
        m.tok, return_type_tok, const_tok);
    if (peek().type == TokenType::kEquals
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kDelete) {
        // = delete; — pure virtual when no ancestor match; removes inherited method when match.
        // Sema enforces: non-virtual `= delete` requires an ancestor match.
        advance(); advance();
        expect(TokenType::kSemicolon, "Expected ';' after '= delete'");
        m.is_delete = true;
    } else if (peek().type == TokenType::kEquals
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kDefault) {
        // = default; — inherits base impl with no-shadow contract. Sema enforces ancestor match.
        advance(); advance();
        expect(TokenType::kSemicolon, "Expected ';' after '= default'");
        m.is_default = true;
    } else if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — no body
    } else {
        std::vector<std::string> param_names;
        for (auto& p : m.params) param_names.push_back(p.second);
        std::string saved_fn = current_function_name_;
        current_function_name_ = class_name.empty()
            ? m.name : (class_name + ":" + m.name);
        m.body = parseBlock(param_names);
        current_function_name_ = saved_fn;
    }
    return m;
}

SlidDef Parser::parseSlidDef(const std::string& base_name) {
    [[maybe_unused]] int t_start = pos_;
    SlidDef slid;
    // Set early (the `Base :` prefix is consumed by the caller) so the field
    // seeding below can merge the base's fields.
    slid.base_name = base_name;
    int name_tok = pos_;
    slid.name = peek().value;
    slid.name_file_id = file_id_;
    slid.name_tok = name_tok;
    advance(); // consume class name

    // a closed class accepts no more *field* reopens — but an empty-`()` reopen
    // (`Name() { ... }`) adds only declarations/methods, no fields, and is the
    // post-`Class()` replacement for the old bare-block reopen, so it is always
    // allowed.
    {
        bool empty_tuple_reopen = peek().type == TokenType::kLParen
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kRParen;
        auto cit = closed_classes_.find(slid.name);
        if (cit != closed_classes_.end() && !empty_tuple_reopen) {
            throw CompileError{file_id_, name_tok,
                "Class '" + slid.name
                    + "' is already complete; further field reopens are not permitted."}
                .addNote(cit->second.file_id, cit->second.tok, "Class was completed here.");
        }
    }

    // save outer's alias map; nested slids in this slid's body register short→canonical here
    auto saved_alias = nested_alias_;
    nested_alias_.clear();

    // template type parameters: Vector<T> or Pair<K, V>
    if (peek().type == TokenType::kLt) {
        advance(); // consume '<'
        while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
            slid.type_params.push_back(
                expect(TokenType::kIdentifier, "Expected type parameter name").value);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kGt, "Expected '>' after type parameters");
    }

    // A template class's method bodies are template context — local classes
    // declared there are carried with the class for per-instantiation
    // materialization.
    bool saved_tmpl = in_template_;
    if (!slid.type_params.empty()) in_template_ = true;

    // parse tuple: (type field_ = default, ...)
    expect(TokenType::kLParen, "Expected '('");

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
        // Inferred-type field: a bare IDENT followed by `=`, `,`, or `)`
        // (no type token). The `=` form has a default; the others trip the
        // semantic-pass "no type and no initializer" error. Structural pivot
        // only — never on naming convention.
        if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kEquals
                || tokens_[pos_ + 1].type == TokenType::kComma
                || tokens_[pos_ + 1].type == TokenType::kRParen)) {
            int field_tok = pos_;
            f.name = advance().value;
            f.file_id = file_id_;
            f.tok = field_tok;
            rejectReserved(file_id_, field_tok, f.name, "field");
            f.type = "";  // sentinel — filled by inferFieldTypes
            if (peek().type == TokenType::kEquals) {
                advance(); // consume '='
                f.default_val = parseExpr();
            }
            slid.fields.push_back(std::move(f));
            if (peek().type == TokenType::kComma) advance();
            continue;
        }
        f.type = parseTypeName();
        int field_tok = pos_;
        f.name = expect(TokenType::kIdentifier, "Expected field name").value;
        f.file_id = file_id_;
        f.tok = field_tok;
        rejectReserved(file_id_, field_tok, f.name, "field");
        // inline fixed-size array field: char name_[16]
        if (peek().type == TokenType::kLBracket
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIntLiteral) {
            advance(); // consume [
            std::string sz = advance().value; // consume N
            expect(TokenType::kRBracket, "Expected ']'");
            f.type += "[" + sz + "]";
        }
        if (peek().type == TokenType::kEquals) {
            advance();
            f.default_val = parseExpr();
        }
        slid.fields.push_back(std::move(f));
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "Expected ')'");

    // (P1) class header+body merge — class field name cannot equal class name.
    for (auto& f : slid.fields) {
        if (f.name == slid.name) {
            throw CompileError{f.file_id, f.tok,
                "Class field '" + f.name + "' conflicts with the class name."}
                .addNote(slid.name_file_id, slid.name_tok, "Class declared here.");
        }
    }
    // (P1) class field names must be unique within the tuple.
    {
        std::map<std::string, FieldRef> first_seen;
        for (auto& f : slid.fields) {
            auto [it, inserted] = first_seen.emplace(f.name, FieldRef{f.file_id, f.tok});
            if (!inserted) {
                throw CompileError{f.file_id, f.tok,
                    "Duplicate class field '" + f.name + "' in class '" + slid.name + "'."}
                    .addNote(it->second.file_id, it->second.tok, "First declared here.");
            }
        }
    }

    // record field names to prevent method-body field assignments from being mistaken for inferred declarations.
    // a reopen seeds from the class's accumulated fields so method bodies see fields declared in earlier occurrences.
    current_slid_fields_.clear();
    auto prior_fields = all_slid_fields_.find(slid.name);
    if (prior_fields != all_slid_fields_.end())
        current_slid_fields_ = prior_fields->second;
    // a derived class's method bodies see the base's fields too — seed them so
    // an inherited-field assignment isn't mistaken for an inferred declaration.
    // all_slid_fields_[base] already accumulates the base's own + inherited
    // fields, so a single merge covers the whole chain.
    if (!slid.base_name.empty()) {
        auto base_fields = all_slid_fields_.find(slid.base_name);
        if (base_fields != all_slid_fields_.end())
            for (auto& [fname, fref] : base_fields->second)
                current_slid_fields_.emplace(fname, fref);
    }
    for (auto& f : slid.fields)
        current_slid_fields_.emplace(f.name, FieldRef{f.file_id, f.tok});
    all_slid_fields_[slid.name] = current_slid_fields_;

    // parse body: methods and definitions
    expect(TokenType::kLBrace, "Expected '{'");

    // alias frame for the class body — an `alias` declared here is visible to
    // the body's methods (nested below this frame) and popped at the close.
    alias_stack_.push_back({});

    auto ctor_body = make<BlockStmt>(t_start);
    bool has_ctor_code = false;

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        // class-internal global declaration. Long form `global [Name] (...) {...}`
        // attaches under the class's namespace; short form `global TYPE NAME = EXPR;`
        // is not used inside class bodies per the spec.
        if (peek().type == TokenType::kGlobal) {
            int global_tok = pos_;
            if (pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kSemicolon)
                errorAt(global_tok, "Global lifetime statement is only allowed in `main`.");
            pending_globals_.push_back(parseGlobalDef(slid.name, ""));
            continue;
        }
        // class-scoped nested enum: `enum Name (values);` — its values are
        // scoped to this class (registered as Class:value), not file scope.
        if (peek().type == TokenType::kEnum) {
            slid.nested_enums.push_back(parseEnumDef());
            continue;
        }
        // class-scope alias: `alias Name = TypeExpr;` — registered in the
        // class body's alias frame, visible to this class's methods.
        if (peek().type == TokenType::kAlias) {
            parseAliasDecl();
            continue;
        }
        // class-scope const declaration: const [type] name = expr;
        // BUT: a leading `const` followed by an elided-return method shape
        // (`_(`, `~(`, `op<sym>(`) is the method-const marker, not a const decl.
        if (peek().type == TokenType::kConst) {
            bool const_is_method_marker = false;
            if (pos_ + 1 < (int)tokens_.size()) {
                const Token& after = tokens_[pos_ + 1];
                if (after.type == TokenType::kBitNot
                    && pos_ + 2 < (int)tokens_.size()
                    && tokens_[pos_ + 2].type == TokenType::kLParen) {
                    const_is_method_marker = true; // const ~()
                } else if (after.type == TokenType::kIdentifier && after.value == "_"
                    && pos_ + 2 < (int)tokens_.size()
                    && tokens_[pos_ + 2].type == TokenType::kLParen) {
                    const_is_method_marker = true; // const _()
                } else if (after.type == TokenType::kOp) {
                    const_is_method_marker = true; // const op<sym>(...)
                }
            }
            if (!const_is_method_marker) {
                slid.consts.push_back(parseConstDef());
                continue;
            }
            // fall through — ctor/dtor branches and isMethodDecl peek past const.
        }
        // explicit constructor: _() { ... }  or forward decl: _();
        // optional leading `const` marker (non-binding for now).
        {
            int probe = pos_;
            int probe_const_tok = -1;
            if (probe < (int)tokens_.size() && tokens_[probe].type == TokenType::kConst) {
                probe_const_tok = probe;
                probe++;
            }
            if (probe + 1 < (int)tokens_.size()
                && tokens_[probe].type == TokenType::kIdentifier
                && tokens_[probe].value == "_"
                && tokens_[probe + 1].type == TokenType::kLParen) {
                if (probe_const_tok >= 0) advance(); // consume const
                int ctor_tok = pos_;
                advance(); // consume _
                expect(TokenType::kLParen, "Expected '('");
                expect(TokenType::kRParen, "Expected ')'");
                if (slid.has_explicit_ctor_decl) {
                    throw CompileError{file_id_, ctor_tok,
                        "Constructor is already defined in class '" + slid.name + "'."}
                        .addNote(slid.explicit_ctor_file_id, slid.explicit_ctor_tok,
                                 "First defined here.");
                }
                slid.has_explicit_ctor_decl = true; // declared — consumer must call ctor
                slid.explicit_ctor_file_id = file_id_;
                slid.explicit_ctor_tok = ctor_tok;
                if (probe_const_tok >= 0) slid.is_const_ctor = true;
                if (peek().type == TokenType::kSemicolon) {
                    advance(); // forward declaration only
                } else {
                    slid.explicit_ctor_body = parseBlock();
                }
                continue;
            }
        }
        // explicit destructor: ~() { ... }  or forward decl: ~();  or virtual ~() { ... }
        // optional leading `const` marker (non-binding for now).
        {
            int probe = pos_;
            int probe_const_tok = -1;
            bool probe_virtual = false;
            if (probe < (int)tokens_.size() && tokens_[probe].type == TokenType::kConst) {
                probe_const_tok = probe;
                probe++;
            }
            if (probe < (int)tokens_.size() && tokens_[probe].type == TokenType::kVirtual) {
                probe_virtual = true;
                probe++;
            }
            if (probe + 1 < (int)tokens_.size()
                && tokens_[probe].type == TokenType::kBitNot
                && tokens_[probe + 1].type == TokenType::kLParen) {
                if (probe_const_tok >= 0) advance(); // consume const
                if (probe_virtual) advance(); // consume virtual
                int dtor_tok = pos_;
                advance(); // consume ~
                expect(TokenType::kLParen, "Expected '('");
                expect(TokenType::kRParen, "Expected ')'");
                if (slid.has_explicit_dtor_decl) {
                    throw CompileError{file_id_, dtor_tok,
                        "Destructor is already defined in class '" + slid.name + "'."}
                        .addNote(slid.explicit_dtor_file_id, slid.explicit_dtor_tok,
                                 "First defined here.");
                }
                slid.has_explicit_dtor_decl = true; // declared — consumer must call dtor
                slid.explicit_dtor_file_id = file_id_;
                slid.explicit_dtor_tok = dtor_tok;
                if (probe_virtual) slid.dtor_is_virtual = true;
                if (probe_const_tok >= 0) slid.is_const_dtor = true;
                if (peek().type == TokenType::kSemicolon) {
                    advance(); // forward declaration only
                } else {
                    slid.dtor_body = parseBlock();
                }
                continue;
            }
        }
        // method definition: starts with a type name followed by identifier followed by (
        // also handles operator methods: op=(...)  op<-(...)  op+(...)  etc.
        // a leading `virtual` is part of the method decl — peek past it without
        // moving pos_ (so isNestedSlidDecl, which runs first, isn't confused).
        int virt_off = (peek().type == TokenType::kVirtual) ? 1 : 0;
        auto isMethodDecl = [&]() {
            int base = pos_ + virt_off;
            // skip a leading const/mutable qualifier on the return type
            while (base < (int)tokens_.size()
                   && (tokens_[base].type == TokenType::kConst
                       || tokens_[base].type == TokenType::kMutable)) {
                base++;
            }
            // skip a paren-qualified return type like (const T)^
            if (base < (int)tokens_.size() && tokens_[base].type == TokenType::kLParen) {
                int depth = 1, scan = base + 1;
                while (scan < (int)tokens_.size() && depth > 0) {
                    if (tokens_[scan].type == TokenType::kLParen) depth++;
                    else if (tokens_[scan].type == TokenType::kRParen) depth--;
                    if (depth == 0) break;
                    scan++;
                }
                if (scan < (int)tokens_.size()) base = scan + 1;
            }
            if (base >= (int)tokens_.size()) return false;
            const Token& t0 = tokens_[base];
            // op<symbol>( without explicit return type
            if (t0.type == TokenType::kOp) {
                auto sym = peekOpSymbolAt(base - pos_ + 1);
                if (!sym) return false;
                int op_end = base + 1 + (*sym == "[]" ? 2 : 1);
                return op_end < (int)tokens_.size() && tokens_[op_end].type == TokenType::kLParen;
            }
            // regular method: return-type name(
            // return type may include pointer/iterator suffixes: ^ or []
            // (paren-qualified types already advanced `base` past the closing paren)
            int name_pos;
            if (tokens_[base].type == TokenType::kBitXor
                || tokens_[base].type == TokenType::kXorXor
                || (tokens_[base].type == TokenType::kLBracket
                    && base + 1 < (int)tokens_.size()
                    && tokens_[base + 1].type == TokenType::kRBracket)) {
                name_pos = base; // paren-qualified — skip suffixes from current pos
            } else {
                if (!(isTypeName(t0) || isUserTypeName(t0))) return false;
                name_pos = base + 1;
            }
            while (name_pos < (int)tokens_.size()) {
                if (tokens_[name_pos].type == TokenType::kBitXor
                    || tokens_[name_pos].type == TokenType::kXorXor) {
                    name_pos++;
                } else if (tokens_[name_pos].type == TokenType::kLBracket
                           && name_pos + 1 < (int)tokens_.size()
                           && tokens_[name_pos + 1].type == TokenType::kRBracket) {
                    name_pos += 2;
                } else {
                    break;
                }
            }
            // optional `const` (method-const marker) between return type and name
            if (name_pos < (int)tokens_.size()
                && tokens_[name_pos].type == TokenType::kConst) {
                name_pos++;
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
        if (isSlidDeclLookahead()) {
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
            auto method = parseMethodDef(slid.name);
            // (P1) class scope merges name + body — method name cannot equal class name.
            if (method.name == slid.name) {
                errorAt(method_tok, "Method '" + method.name + "' shares the name of its enclosing class.");
            }
            slid.methods.push_back(std::move(method));
        } else {
            // A class body holds only definitions — methods, nested classes,
            // enums, aliases, constants. Executable statements belong in '_()'.
            errorHere("Executable code is not allowed directly in a class "
                      "body; put statements in the constructor '_()'.");
        }
    }

    if (has_ctor_code)
        slid.ctor_body = std::move(ctor_body);
    alias_stack_.pop_back();

    int close_tok = pos_;
    expect(TokenType::kRBrace, "Expected '}'");
    current_slid_fields_.clear();
    nested_alias_ = std::move(saved_alias);

    // a name cannot be both a class and a namespace.
    auto ns_clash = seen_namespaces_.find(slid.name);
    if (ns_clash != seen_namespaces_.end())
        throw CompileError{slid.name_file_id, slid.name_tok,
            "'" + slid.name + "' is a namespace, not a class. Reopen with '"
                + slid.name + "', no parentheses."}
            .addNote(ns_clash->second.file_id, ns_clash->second.tok,
                "Namespace declared here.");

    // register this class as seen (disambiguates lone `(...)` on next reopen)
    // and mark it closed if this reopen has no trailing `...`. Exception: an
    // empty-`()` reopen of an already-seen class is method/declaration-only —
    // it touches no field layout, so it neither closes nor reopens. (A
    // first-occurrence `Foo()` IS a complete field-less class and does close.)
    bool method_only_reopen = seen_classes_.count(slid.name)
        && slid.fields.empty()
        && !slid.has_trailing_ellipsis && !slid.has_leading_ellipsis;
    seen_classes_.emplace(slid.name, FieldRef{slid.name_file_id, slid.name_tok});
    if (!slid.has_trailing_ellipsis && !method_only_reopen)
        closed_classes_[slid.name] = FieldRef{file_id_, close_tok};

    in_template_ = saved_tmpl;
    // Drain local classes collected from a template class's method bodies.
    if (!slid.type_params.empty()) {
        slid.local_classes = std::move(pending_local_classes_);
        pending_local_classes_.clear();
    }

    return slid;
}

GlobalDef Parser::parseGlobalDef(const std::string& namespace_prefix,
                                 const std::string& visible_in_function) {
    GlobalDef g;
    int global_tok = pos_;
    g.file_id = file_id_;
    g.tok = global_tok;
    g.visible_in_function = visible_in_function;
    advance(); // consume 'global'

    // Distinguish long form (anonymous `(...)` or `Name (...)`) from short form
    // (`TYPE NAME = EXPR;`). Long form has `(` either immediately after `global`
    // or after a single identifier; short form has a type token followed by a
    // name identifier.
    bool is_short_form = false;
    std::string optional_name;
    if (peek().type == TokenType::kLParen) {
        // anonymous long form; namespace = prefix
    } else if (peek().type == TokenType::kIdentifier
               && pos_ + 1 < (int)tokens_.size()
               && tokens_[pos_ + 1].type == TokenType::kLParen) {
        optional_name = advance().value;
    } else {
        is_short_form = true;
    }

    // build the namespace name from the prefix and optional name
    if (!namespace_prefix.empty() && !optional_name.empty())
        g.namespace_name = namespace_prefix + ":" + optional_name;
    else if (!namespace_prefix.empty())
        g.namespace_name = namespace_prefix;
    else
        g.namespace_name = optional_name; // "" for anonymous file-scope

    if (is_short_form) {
        // `global NAME = EXPR;` (inferred type) or
        // `global TYPE NAME [= EXPR];` (typed; initializer optional for
        // header-style forward decls).
        FieldDef f;
        bool inferred = peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kEquals;
        if (inferred) {
            int name_tok = pos_;
            f.name = advance().value;
            f.file_id = file_id_;
            f.tok = name_tok;
            f.type = "";  // inferred from const-folded default
            expect(TokenType::kEquals, "Inferred global short form requires an initializer.");
            f.default_val = parseExpr();
        } else {
            f.type = parseTypeName();
            int name_tok = pos_;
            f.name = expect(TokenType::kIdentifier, "Expected variable name after 'global'").value;
            f.file_id = file_id_;
            f.tok = name_tok;
            if (peek().type == TokenType::kLBracket
                && pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kIntLiteral) {
                advance();
                std::string sz = advance().value;
                expect(TokenType::kRBracket, "Expected ']'");
                f.type += "[" + sz + "]";
            }
            if (peek().type == TokenType::kEquals) {
                advance();
                f.default_val = parseExpr();
            }
        }
        expect(TokenType::kSemicolon, "Expected ';' after global short-form declaration.");
        g.fields.push_back(std::move(f));
        return g;
    }

    // long form: (field_list)
    expect(TokenType::kLParen, "Expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        FieldDef f;
        // inferred-type field: bare IDENT followed by '=', ',', or ')'
        if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kEquals
                || tokens_[pos_ + 1].type == TokenType::kComma
                || tokens_[pos_ + 1].type == TokenType::kRParen)) {
            int field_tok = pos_;
            f.name = advance().value;
            f.file_id = file_id_;
            f.tok = field_tok;
            f.type = "";
            if (peek().type == TokenType::kEquals) {
                advance();
                f.default_val = parseExpr();
            }
        } else {
            f.type = parseTypeName();
            int field_tok = pos_;
            f.name = expect(TokenType::kIdentifier, "Expected field name in global declaration.").value;
            f.file_id = file_id_;
            f.tok = field_tok;
            if (peek().type == TokenType::kLBracket
                && pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kIntLiteral) {
                advance();
                std::string sz = advance().value;
                expect(TokenType::kRBracket, "Expected ']'");
                f.type += "[" + sz + "]";
            }
            if (peek().type == TokenType::kEquals) {
                advance();
                f.default_val = parseExpr();
            }
        }
        g.fields.push_back(std::move(f));
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "Expected ')'");

    // Pre-collect field names; ctor and dtor bodies see them as in-scope so
    // bare `field = expr;` is an assignment to the global slid's field, not
    // an inferred-type new local.
    std::vector<std::string> field_names;
    for (auto& f : g.fields) field_names.push_back(f.name);

    // body: '{' optional _() and ~() — each either `;` (forward decl) or
    // `{ ... }` (body). Forward decls record `has_*_decl` but leave the
    // body pointers null; the defining TU supplies the body.
    expect(TokenType::kLBrace, "Expected '{'");
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        // _() { ... }  or  _();
        if (peek().type == TokenType::kIdentifier && peek().value == "_"
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            int ctor_tok = pos_;
            advance(); // _
            expect(TokenType::kLParen, "Expected '('");
            expect(TokenType::kRParen, "Expected ')'");
            if (g.has_ctor_decl)
                errorAt(ctor_tok, "Constructor is already defined for this global slid.");
            g.has_ctor_decl = true;
            if (peek().type == TokenType::kSemicolon) {
                advance(); // forward declaration — body lives elsewhere
            } else {
                g.ctor_body = parseBlock(field_names);
            }
            continue;
        }
        // ~() { ... }  or  ~();
        if (peek().type == TokenType::kBitNot
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            int dtor_tok = pos_;
            advance(); // ~
            expect(TokenType::kLParen, "Expected '('");
            expect(TokenType::kRParen, "Expected ')'");
            if (g.has_dtor_decl)
                errorAt(dtor_tok, "Destructor is already defined for this global slid.");
            g.has_dtor_decl = true;
            if (peek().type == TokenType::kSemicolon) {
                advance(); // forward declaration — body lives elsewhere
            } else {
                g.dtor_body = parseBlock(field_names);
            }
            continue;
        }
        errorHere("Expected '_()', '~()', or '}' in global body.");
    }
    expect(TokenType::kRBrace, "Expected '}'");

    // pair rule — checked against declarations, not bodies. A header-style
    // forward decl that names only one of `_()` / `~()` still trips this.
    if (g.has_ctor_decl && !g.has_dtor_decl)
        errorAt(global_tok, "Global slid has ctor but no dtor.");
    if (g.has_dtor_decl && !g.has_ctor_decl)
        errorAt(global_tok, "Global slid has dtor but no ctor.");

    return g;
}

GlobalDef Parser::parseBareGlobalShortForm() {
    GlobalDef g;
    int decl_tok = pos_;
    g.file_id = file_id_;
    g.tok = decl_tok;
    g.namespace_name = "";

    FieldDef f;
    f.type = parseTypeName();
    int name_tok = pos_;
    f.name = expect(TokenType::kIdentifier, "Expected variable name.").value;
    f.file_id = file_id_;
    f.tok = name_tok;
    if (peek().type == TokenType::kLBracket
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kIntLiteral) {
        advance();
        std::string sz = advance().value;
        expect(TokenType::kRBracket, "Expected ']'");
        f.type += "[" + sz + "]";
    }
    // Initializer optional: header-style decls (`int where_;`) omit it; the
    // defining TU's matching decl carries the value.
    if (peek().type == TokenType::kEquals) {
        advance();
        f.default_val = parseExpr();
    }
    expect(TokenType::kSemicolon, "Expected ';' after global declaration.");
    g.fields.push_back(std::move(f));
    return g;
}

EnumDef Parser::parseEnumDef() {
    [[maybe_unused]] int t_start = pos_;
    expect(TokenType::kEnum, "Expected 'enum'");
    EnumDef e;
    e.name = expect(TokenType::kIdentifier, "Expected enum name").value;
    expect(TokenType::kLParen, "Expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        e.values.push_back(expect(TokenType::kIdentifier, "Expected enum value").value);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "Expected ')'");
    expect(TokenType::kSemicolon, "Expected ';' after enum declaration");
    return e;
}

ExternalMethodDef Parser::parseExternalMethodDef() {
    [[maybe_unused]] int t_start = pos_;
    ExternalMethodDef em;
    int return_type_tok = -1;
    int em_const_tok = -1;
    // optional return type (primitive keyword); absent for ctor/dtor
    if (isTypeName(peek())
        || peek().type == TokenType::kConst
        || peek().type == TokenType::kMutable
        || peek().type == TokenType::kLParen) {
        return_type_tok = pos_;
        em.return_type = parseTypeName();
        em.has_explicit_return = true;
    }
    // optional `const` method marker between return type and class name
    if (peek().type == TokenType::kConst) {
        em_const_tok = pos_;
        em.is_const_method = true;
        advance();
    }
    // TypeName:
    em.slid_name = expect(TokenType::kIdentifier, "Expected class name").value;
    // set field names for this slid so assignments to fields aren't mistaken for inferred declarations
    {
        auto fit = all_slid_fields_.find(em.slid_name);
        current_slid_fields_ = (fit != all_slid_fields_.end()) ? fit->second : std::map<std::string, FieldRef>{};
    }
    expect(TokenType::kColon, "Expected ':'");
    // method name, or _ (ctor), or ~ (dtor)
    em.file_id = file_id_;
    em.tok = pos_;
    if (peek().type == TokenType::kIdentifier && peek().value == "_") {
        em.method_name = "_"; advance();
    } else if (peek().type == TokenType::kBitNot) {
        em.method_name = "~"; advance();
    } else {
        em.method_name = expect(TokenType::kIdentifier, "Expected method name").value;
    }
    expect(TokenType::kLParen, "Expected '('");
    parseParamList(em.params, em.param_mutable, em.param_mut_toks, em.param_defaults);
    expect(TokenType::kRParen, "Expected ')'");
    checkMethodHeadRules(file_id_, em.method_name, (int)em.params.size(),
        em.has_explicit_return, em.is_const_method,
        em.tok, return_type_tok, em_const_tok);
    if (peek().type == TokenType::kEquals
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kDelete) {
        advance(); advance();
        expect(TokenType::kSemicolon, "Expected ';' after '= delete'");
        em.is_delete = true;
    } else if (peek().type == TokenType::kEquals
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kDefault) {
        advance(); advance();
        expect(TokenType::kSemicolon, "Expected ';' after '= default'");
        em.is_default = true;
    } else if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — no body
    } else {
        std::vector<std::string> param_names;
        for (auto& p : em.params) param_names.push_back(p.second);
        std::string saved_fn = current_function_name_;
        current_function_name_ = em.slid_name + ":" + em.method_name;
        em.body = parseBlock(param_names);
        current_function_name_ = saved_fn;
    }
    current_slid_fields_.clear();
    return em;
}

// Namespace block: `Name { members }`, or the shorthand `Name import { … }`
// which desugars to `Name { import { … } }`. A namespace is not a class — no
// fields, no `self`; its members are functions and consts, carried in
// program.functions / program.consts tagged with the namespace name. An
// `import { … }` (sub-)block makes every `;`-declaration inside it a foreign
// C function (is_foreign).
void Parser::parseNamespace(Program& program) {
    int ns_tok = pos_;
    std::string ns_name = expect(TokenType::kIdentifier, "Expected namespace name").value;
    // a name cannot be both a class and a namespace.
    auto cls_clash = seen_classes_.find(ns_name);
    if (cls_clash != seen_classes_.end())
        throw CompileError{file_id_, ns_tok,
            "'" + ns_name + "' is a class, not a namespace. Reopen with '"
                + ns_name + "()'."}
            .addNote(cls_clash->second.file_id, cls_clash->second.tok,
                "Class declared here.");
    seen_namespaces_.emplace(ns_name, FieldRef{file_id_, ns_tok});
    // The namespace OWNS its functions/consts (NamespaceDef.functions/.consts).
    // They are not free functions — keeping them out of program.functions is
    // what stops the free-function duplicate-detector from conflating, e.g.,
    // a global `greet` with `Space:greet`.
    NamespaceDef nd;
    nd.name = ns_name;
    nd.file_id = file_id_;
    nd.tok = ns_tok;

    // `Name import { … }` shorthand: the whole body is one import block.
    bool shorthand_import = (peek().type == TokenType::kImport);
    if (shorthand_import) advance();
    expect(TokenType::kLBrace, "Expected '{'");

    // parse one foreign-import function declaration into the namespace.
    auto parseImportDecl = [&]() {
        FunctionDef fn = parseFunctionDef();
        fn.is_foreign = true;
        fn.namespace_name = ns_name;
        nd.functions.push_back(std::move(fn));
    };

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        if (shorthand_import) {
            parseImportDecl();
        } else if (peek().type == TokenType::kConst) {
            ConstDef cd = parseConstDef();
            cd.namespace_name = ns_name;
            nd.consts.push_back(std::move(cd));
        } else if (peek().type == TokenType::kImport) {
            // import sub-block: `import { ret f(params); … }`
            advance(); // 'import'
            expect(TokenType::kLBrace, "Expected '{' after 'import'");
            while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof)
                parseImportDecl();
            expect(TokenType::kRBrace, "Expected '}'");
        } else if (peek().type == TokenType::kAlias) {
            // function alias: `alias <name> = <target>;` — additive overload
            // merge, resolved in codegen. Recorded on the Program tagged with
            // this namespace; lhs and rhs are looked up qualified by it.
            advance(); // 'alias'
            AliasDef ad;
            ad.namespace_name = ns_name;
            ad.file_id = file_id_;
            ad.tok = pos_;
            ad.name = expect(TokenType::kIdentifier, "Expected alias name after 'alias'").value;
            expect(TokenType::kEquals, "Expected '=' after alias name");
            ad.target = expect(TokenType::kIdentifier, "Expected a function name after '='").value;
            expect(TokenType::kSemicolon, "Expected ';' after alias declaration");
            program.aliases.push_back(std::move(ad));
        } else {
            // a slids namespace function: `ret f(params) = import;` (foreign) or
            // `ret f(params) { body }` (slids). parseFunctionDef handles both.
            FunctionDef fn = parseFunctionDef();
            fn.namespace_name = ns_name;
            nd.functions.push_back(std::move(fn));
        }
    }
    expect(TokenType::kRBrace, "Expected '}'");
    program.namespaces.push_back(std::move(nd));
}

FunctionDef Parser::parseFunctionDef() {
    [[maybe_unused]] int t_start = pos_;
    FunctionDef fn;
    // A `(` followed by `const`/`mutable` is a paren-qualified return type
    // (`(const char)[] f()`), not a named-tuple return list.
    bool paren_qual_return = peek().type == TokenType::kLParen
        && pos_ + 1 < (int)tokens_.size()
        && (tokens_[pos_ + 1].type == TokenType::kConst
            || tokens_[pos_ + 1].type == TokenType::kMutable);
    if (peek().type == TokenType::kLParen && !paren_qual_return) {
        advance(); // consume '('
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            std::string type = parseTypeName();
            std::string name = expect(TokenType::kIdentifier, "Expected field name").value;
            fn.tuple_return_fields.emplace_back(type, name);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "Expected ')'");
    } else {
        fn.return_type = parseTypeName();
    }
    {
        int fname_tok = pos_;
        if (peek().type == TokenType::kOp) {
            auto sym = peekOpSymbolAt(1);
            errorAt(fname_tok, sym
                ? "Operator 'op" + *sym
                    + "' must be a method of a class; free-function operators are not allowed."
                : "The keyword 'op' is reserved; free-function operators are not allowed.");
        }
        std::string fname = expect(TokenType::kIdentifier, "Expected function name").value;
        fn.name = fname;
        fn.user_name = fname;
        fn.file_id = file_id_;
        fn.tok = fname_tok;
    }
    // template type params: funcname<T, U, ...>
    if (peek().type == TokenType::kLt && isTemplateCallLookahead()) {
        advance(); // consume '<'
        while (peek().type != TokenType::kGt && peek().type != TokenType::kEof) {
            fn.type_params.push_back(
                expect(TokenType::kIdentifier, "Expected type parameter name").value);
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kGt, "Expected '>'");
    }
    expect(TokenType::kLParen, "Expected '('");
    int params_tok = pos_;
    parseParamList(fn.params, fn.param_mutable, fn.param_mut_toks, fn.param_defaults);
    expect(TokenType::kRParen, "Expected ')'");
    // (P1) function header+body merge — parameter name cannot equal function name.
    for (auto& p : fn.params) {
        if (p.second == fn.user_name) {
            errorAt(params_tok, "Parameter '" + p.second + "' shares the name of its enclosing function.");
        }
    }
    if (peek().type == TokenType::kEquals
        && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kImport) {
        // `= import;` — foreign C function: bare symbol, C ABI, no slids body.
        advance(); advance();
        expect(TokenType::kSemicolon, "Expected ';' after '= import'");
        fn.is_foreign = true;
    } else if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — body remains null
    } else {
        std::vector<std::string> param_names;
        for (auto& p : fn.params) param_names.push_back(p.second);
        std::string saved_fn = current_function_name_;
        current_function_name_ = fn.user_name;
        bool saved_tmpl = in_template_;
        if (!fn.type_params.empty()) in_template_ = true;
        fn.body = parseBlock(param_names);
        in_template_ = saved_tmpl;
        current_function_name_ = saved_fn;
        // Drain local classes collected from a template function body.
        if (!fn.type_params.empty()) {
            fn.local_classes = std::move(pending_local_classes_);
            pending_local_classes_.clear();
        }
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
            std::string module = expect(TokenType::kIdentifier, "Expected module name after 'import'").value;
            expect(TokenType::kSemicolon, "Expected ';' after import");

            std::vector<std::string> search;
            for (auto& p : import_paths_) search.push_back(p);
            if (!source_dir_.empty()) search.push_back(source_dir_);
            if (search.empty()) search.push_back("");

            std::string header_path = findHeader(module, search);
            if (header_path.empty())
                throw CompileError{-1, 0, std::string("Cannot find '" + module + ".slh' on the import path.")};

            // import-once: skip if this header has already been loaded in this compile
            if (!imported_once_->insert(header_path).second) continue;

            program.imported_headers.push_back(header_path);
            std::ifstream in(header_path);
            std::ostringstream buf; buf << in.rdbuf();
            int hdr_file_id = sm_.openFile(header_path, buf.str(), file_id_);
            Lexer hdr_lexer(sm_, hdr_file_id);
            Parser hdr_parser(sm_, hdr_file_id, hdr_lexer.tokenize(), source_dir_, import_paths_, imported_once_);
            Program hdr = hdr_parser.parse();

            // Fold the header parser's class field sets into ours. A `.sl` that
            // reopens an imported class must be able to tell a header-declared
            // field assignment from an inferred declaration — parseSlid() seeds
            // current_slid_fields_ from all_slid_fields_, which is otherwise
            // only populated from class blocks in the current file.
            for (auto& [cname, fields] : hdr_parser.all_slid_fields_)
                for (auto& [fname, fref] : fields)
                    all_slid_fields_[cname].emplace(fname, fref);

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
            // Imported globals: stamp the source module and fold into the
            // consumer's registry. Post-parse dedup drops any imported entry
            // that the consuming TU also defines locally.
            for (auto& g : hdr.globals) {
                g.impl_module = module;
                program.globals.push_back(std::move(g));
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
        // type alias declaration: alias Name = TypeExpr;
        else if (peek().type == TokenType::kAlias) {
            parseAliasDecl();
        }
        // global slid declaration at file scope. `global;` is a lifetime
        // statement and only allowed in main — reject here.
        else if (peek().type == TokenType::kGlobal) {
            int global_tok = pos_;
            if (pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kSemicolon) {
                errorAt(global_tok, "Global lifetime statement is only allowed in `main`.");
            }
            program.globals.push_back(parseGlobalDef("", ""));
        }
        // const declaration: const [type] name = expr;
        else if (peek().type == TokenType::kConst) {
            program.consts.push_back(parseConstDef());
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
            expect(TokenType::kGt, "Expected '>'");
            std::vector<std::string> param_types;
            expect(TokenType::kLParen, "Expected '(' after instantiate type args");
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                param_types.push_back(parseTypeName());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "Expected ')'");
            expect(TokenType::kSemicolon, "Expected ';'");
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
                SlidDef slid = parseSlidDef(base_name);
                recordSlidMethods(slid);
                program.slids.push_back(std::move(slid));
            }
        }
        // namespace block: `Name { ... }`  or shorthand `Name import { ... }`.
        // (Class reopens now require `()` — `Class() { ... }` — and route to
        // the slid-def path above.)
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kLBrace
                || (tokens_[pos_ + 1].type == TokenType::kImport
                    && pos_ + 2 < (int)tokens_.size()
                    && tokens_[pos_ + 2].type == TokenType::kLBrace))) {
            parseNamespace(program);
        }
        // bare file-scope short-form global: TYPE NAME [= EXPR];
        // shape requires a type token then identifier then `=` or `;`.
        // Header-style decls omit the initializer; the defining TU's matching
        // decl carries the value. The kIdentifier case can only land here
        // once the slid-def / derived-class / external method / external
        // method block paths above have already declined.
        else if ((isTypeName(peek()) || peek().type == TokenType::kIdentifier)
                 && pos_ + 2 < (int)tokens_.size()
                 && tokens_[pos_ + 1].type == TokenType::kIdentifier
                 && (tokens_[pos_ + 2].type == TokenType::kEquals
                     || tokens_[pos_ + 2].type == TokenType::kSemicolon
                     || tokens_[pos_ + 2].type == TokenType::kLBracket)) {
            program.globals.push_back(parseBareGlobalShortForm());
        }
        // bare file-scope unnamed instance: `Name;` declares an unnamed global.
        // One operation — lexical scope (file scope) makes it a global. It is
        // never a forward declaration: slids has no explicit forward class
        // decls. Construction is eager at main's `global;`; see codegen.
        else if (peek().type == TokenType::kIdentifier
                 && pos_ + 1 < (int)tokens_.size()
                 && tokens_[pos_ + 1].type == TokenType::kSemicolon) {
            int name_tok = pos_;
            std::string tname = advance().value; // type name
            advance();                           // ';'
            program.unnamed_globals.push_back({tname, file_id_, name_tok});
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
                // `ret Ns:fn(...) { ... }` — if Ns is a declared namespace, this
                // is an external definition of a namespace function, not a
                // class method. Route it into the namespace's own function list
                // (so it never lands in external_methods / synthesizes a slid).
                NamespaceDef* ns = nullptr;
                for (auto& n : program.namespaces)
                    if (n.name == tokens_[p].value) { ns = &n; break; }
                if (ns) {
                    ExternalMethodDef em = parseExternalMethodDef();
                    FunctionDef fn;
                    fn.return_type    = em.return_type;
                    fn.name           = em.method_name;
                    fn.user_name      = em.method_name;
                    fn.params         = std::move(em.params);
                    fn.param_mutable  = std::move(em.param_mutable);
                    fn.param_mut_toks = std::move(em.param_mut_toks);
                    fn.param_defaults = std::move(em.param_defaults);
                    fn.body           = std::move(em.body);
                    fn.file_id        = em.file_id;
                    fn.tok            = em.tok;
                    fn.namespace_name = ns->name;
                    ns->functions.push_back(std::move(fn));
                } else {
                    program.external_methods.push_back(parseExternalMethodDef());
                }
            } else {
                program.functions.push_back(parseFunctionDef());
            }
        }
        // Drain any globals collected by the just-parsed top-level decl.
        for (auto& g : pending_globals_) program.globals.push_back(std::move(g));
        pending_globals_.clear();
        // Drain local classes collected from function bodies. They already
        // carry a unique canonical name; the hoist pass below flattens any
        // classes nested inside them.
        for (auto& s : pending_slids_) {
            recordSlidMethods(s);
            program.slids.push_back(std::move(s));
        }
        pending_slids_.clear();
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

    // (P2) header/def dedup. When this TU imports a header that re-declares
    // a global the TU itself defines, the header's GlobalDef is redundant —
    // the defining TU's bodies / initializers / lazy helpers are canonical.
    // Drop wholly-covered imported entries; flag partial overlap (some fields
    // local, others imported under the same namespace) as a header/def
    // mismatch.
    {
        std::set<std::pair<std::string,std::string>> local_pairs;
        for (auto& g : program.globals)
            if (g.impl_module.empty())
                for (auto& f : g.fields)
                    local_pairs.emplace(g.namespace_name, f.name);
        std::vector<GlobalDef> filtered;
        filtered.reserve(program.globals.size());
        for (auto& g : program.globals) {
            if (!g.impl_module.empty()) {
                int covered = 0;
                for (auto& f : g.fields)
                    if (local_pairs.count({g.namespace_name, f.name}))
                        covered++;
                int total = (int)g.fields.size();
                if (covered == total) continue; // fully redefined locally — drop
                if (covered > 0) {
                    std::string ns = g.namespace_name.empty() ? "<unnamed>" : g.namespace_name;
                    throw CompileError{g.file_id, g.tok,
                        "Imported global namespace '" + ns
                            + "' has fields partially redefined in this TU; the local and the header must agree on the full field set."};
                }
            }
            filtered.push_back(std::move(g));
        }
        program.globals = std::move(filtered);
    }

    // (P1) global slids: field-name collision within a namespace. Co-named
    // global slids stack in the same namespace; their field names must all
    // be distinct. Detection is intra-TU; cross-TU collisions are caught by
    // the link-time aggregator.
    {
        struct Site { std::string ns; int file_id; int tok; };
        // key: "<namespace>:<field>"; value: first-seen site.
        std::map<std::string, Site> seen;
        for (auto& g : program.globals) {
            for (auto& f : g.fields) {
                std::string key = g.namespace_name + "\x01" + f.name;
                auto [it, inserted] = seen.emplace(key, Site{g.namespace_name, f.file_id, f.tok});
                if (!inserted) {
                    std::string ns_label = g.namespace_name.empty() ? "<unnamed>" : g.namespace_name;
                    throw CompileError{f.file_id, f.tok,
                        "Global namespace '" + ns_label + "' redeclares field '" + f.name + "'."}
                        .addNote(it->second.file_id, it->second.tok, "First declared here.");
                }
            }
        }
    }

    // (P1) auto-insert `global;` at the top of main's body when absent. Walks
    // every nested block in main looking for an existing GlobalLifetimeStmt
    // — the user-supplied form may be in a nested scope (the customized usage
    // pattern). When none is found, prepend a synthetic one.
    {
        FunctionDef* main_fn = nullptr;
        for (auto& fn : program.functions) {
            if (fn.user_name == "main" && fn.body) { main_fn = &fn; break; }
        }
        if (main_fn) {
            std::function<bool(BlockStmt&)> contains_lifetime =
                [&](BlockStmt& block) {
                    for (auto& s : block.stmts) {
                        if (dynamic_cast<GlobalLifetimeStmt*>(s.get())) return true;
                        if (auto* b = dynamic_cast<BlockStmt*>(s.get()))
                            if (contains_lifetime(*b)) return true;
                        if (auto* iff = dynamic_cast<IfStmt*>(s.get())) {
                            if (iff->then_block && contains_lifetime(*iff->then_block)) return true;
                            if (iff->else_block && contains_lifetime(*iff->else_block)) return true;
                        }
                        if (auto* w = dynamic_cast<WhileStmt*>(s.get())) {
                            if (w->body && contains_lifetime(*w->body)) return true;
                        }
                        if (auto* fl = dynamic_cast<ForLongStmt*>(s.get())) {
                            if (fl->body && contains_lifetime(*fl->body)) return true;
                        }
                    }
                    return false;
                };
            if (!contains_lifetime(*main_fn->body)) {
                auto synth = std::make_unique<GlobalLifetimeStmt>();
                synth->file_id = main_fn->file_id;
                synth->tok = main_fn->tok;
                main_fn->body->stmts.insert(main_fn->body->stmts.begin(),
                                            std::move(synth));
            }

            // (P6) at most one `global;` per program. Walk main's body
            // recursively counting user-written GlobalLifetimeStmt; auto-
            // insert is suppressed when a user wrote any, so the count is
            // purely user count. >1 means the user wrote a second instance.
            struct Found { int file_id; int tok; };
            std::vector<Found> sites;
            std::function<void(BlockStmt&)> collect =
                [&](BlockStmt& block) {
                    for (auto& s : block.stmts) {
                        if (auto* gls = dynamic_cast<GlobalLifetimeStmt*>(s.get()))
                            sites.push_back({gls->file_id, gls->tok});
                        if (auto* b = dynamic_cast<BlockStmt*>(s.get()))
                            collect(*b);
                        if (auto* iff = dynamic_cast<IfStmt*>(s.get())) {
                            if (iff->then_block) collect(*iff->then_block);
                            if (iff->else_block) collect(*iff->else_block);
                        }
                        if (auto* w = dynamic_cast<WhileStmt*>(s.get())) {
                            if (w->body) collect(*w->body);
                        }
                        if (auto* fl = dynamic_cast<ForLongStmt*>(s.get())) {
                            if (fl->body) collect(*fl->body);
                            if (fl->update_block) collect(*fl->update_block);
                        }
                        if (auto* sw = dynamic_cast<SwitchStmt*>(s.get())) {
                            for (auto& c : sw->cases)
                                for (auto& cst : c.stmts) {
                                    if (auto* b = dynamic_cast<BlockStmt*>(cst.get()))
                                        collect(*b);
                                    else if (auto* gls = dynamic_cast<GlobalLifetimeStmt*>(cst.get()))
                                        sites.push_back({gls->file_id, gls->tok});
                                }
                        }
                    }
                };
            collect(*main_fn->body);
            if ((int)sites.size() > 1) {
                throw CompileError{sites[1].file_id, sites[1].tok,
                    "Global lifetime statement `global;` cannot appear more than once in `main`."}
                    .addNote(sites[0].file_id, sites[0].tok,
                             "First declared here.");
            }
        }
    }

    // (P1) bare-enum values inject into the enclosing (file) scope. Any
    // file-scope identifier (function name, slid name, enum type, or sibling
    // enum value) that equals an enum value is a same-scope collision.
    {
        std::map<std::string, std::string> enum_values; // value -> owning enum
        for (auto& e : program.enums) {
            for (auto& v : e.values) {
                auto ins = enum_values.emplace(v, e.name);
                if (!ins.second) {
                    throw CompileError{-1, 0, std::string("Enum value '" + v + "' appears in both '"
                        + ins.first->second + "' and '" + e.name + "'.")};
                }
            }
        }
        for (auto& fn : program.functions) {
            auto it = enum_values.find(fn.user_name);
            if (it != enum_values.end()) {
                throw CompileError{-1, 0, std::string("Function '" + fn.user_name
                    + "' collides with an enum value from '" + it->second + "'.")};
            }
        }
        for (auto& s : program.slids) {
            auto it = enum_values.find(s.name);
            if (it != enum_values.end()) {
                throw CompileError{-1, 0, std::string("Class or namespace '" + s.name
                    + "' collides with an enum value from '" + it->second + "'.")};
            }
        }
        for (auto& e : program.enums) {
            auto it = enum_values.find(e.name);
            if (it != enum_values.end() && it->second != e.name) {
                throw CompileError{-1, 0, std::string("Enum type '" + e.name
                    + "' collides with an enum value from '" + it->second + "'.")};
            }
        }
    }

    // (P1) file-scope function uniqueness — at most one definition per
    // signature. Forward declarations (body == nullptr) are pair-able with a
    // matching definition and don't count as duplicates. Namespace functions
    // (program.namespaces[].functions) are keyed by their `ns:name` qualified
    // form, so `Space:greet` never collides with a global `greet` but two
    // `Space:greet` of the same signature do.
    {
        struct Site { std::string raw_key; int file_id; int tok; };
        std::map<std::string, Site> sigs;
        auto check = [&](const FunctionDef& fn) {
            if (!fn.body) return;
            std::string qname = qualifiedName(fn.namespace_name, fn.user_name);
            std::string raw = qname + "(";
            for (size_t i = 0; i < fn.params.size(); i++) {
                raw += fn.params[i].first;
                if (i < fn.param_mutable.size() && fn.param_mutable[i]) raw += "!mut";
                raw += ",";
            }
            raw += ")";
            std::string canon = qname + "(";
            for (auto& p : fn.params) canon += canonicalType(p.first) + ",";
            canon += ")";
            auto [it, inserted] = sigs.emplace(canon, Site{raw, fn.file_id, fn.tok});
            if (!inserted) {
                std::string msg = (it->second.raw_key == raw)
                    ? "Function '" + qname + "' is redefined with the same signature."
                    : "Function '" + qname + "' is redefined with the same signature without qualifiers.";
                throw CompileError{fn.file_id, fn.tok, msg}
                    .addNote(it->second.file_id, it->second.tok, "First defined here.");
            }
        };
        for (auto& fn : program.functions) check(fn);
        for (auto& ns : program.namespaces)
            for (auto& fn : ns.functions) check(fn);
    }

    // (P1.5) decl-def `mutable` agreement for free functions. A forward
    // declaration's `mutable` annotations must match the definition exactly
    // (per-slot). Same canonical signature but differing mutable bits means
    // the publisher and the implementor disagree on which params accept
    // writes — never silent.
    {
        struct Site { std::string raw_key; int file_id; int tok; };
        std::map<std::string, Site> first;
        for (auto& fn : program.functions) {
            std::string canon = fn.user_name + "(";
            for (auto& p : fn.params) canon += canonicalType(p.first) + ",";
            canon += ")";
            std::string raw = fn.user_name + "(";
            for (size_t i = 0; i < fn.params.size(); i++) {
                raw += fn.params[i].first;
                if (i < fn.param_mutable.size() && fn.param_mutable[i]) raw += "!mut";
                raw += ",";
            }
            raw += ")";
            auto [it, inserted] = first.emplace(canon, Site{raw, fn.file_id, fn.tok});
            if (!inserted && it->second.raw_key != raw) {
                throw CompileError{fn.file_id, fn.tok,
                    "Function '" + fn.user_name +
                    "' declaration and definition disagree on 'mutable' annotation."}
                    .addNote(it->second.file_id, it->second.tok, "First declared here.");
            }
        }
    }

    return program;
}

void Parser::mergeReopens(Program& program) {
    [[maybe_unused]] int t_start = pos_;
    // group SlidDef indices by class name in source order. skip template
    // slids (have separate machinery).
    std::map<std::string, std::vector<int>> groups;
    for (int i = 0; i < (int)program.slids.size(); i++) {
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
            // OR — "any reopen contributed this". Carry the first-set tok forward
            // so later cross-reopen duplicate detection (if added) can attribute correctly.
            if (!dst.has_explicit_ctor_decl && src.has_explicit_ctor_decl) {
                dst.explicit_ctor_file_id = src.explicit_ctor_file_id;
                dst.explicit_ctor_tok    = src.explicit_ctor_tok;
            }
            if (!dst.has_explicit_dtor_decl && src.has_explicit_dtor_decl) {
                dst.explicit_dtor_file_id = src.explicit_dtor_file_id;
                dst.explicit_dtor_tok    = src.explicit_dtor_tok;
            }
            dst.has_explicit_ctor_decl = dst.has_explicit_ctor_decl || src.has_explicit_ctor_decl;
            dst.has_explicit_dtor_decl = dst.has_explicit_dtor_decl || src.has_explicit_dtor_decl;
            dst.is_const_ctor          = dst.is_const_ctor          || src.is_const_ctor;
            dst.is_const_dtor          = dst.is_const_dtor          || src.is_const_dtor;
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
            // accumulate class-scoped nested enums across reopens
            for (auto& e : src.nested_enums) dst.nested_enums.push_back(std::move(e));
            // accumulate class-scoped consts across reopens
            for (auto& c : src.consts) dst.consts.push_back(std::move(c));
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
        struct Site { std::string raw_key; int file_id; int tok; };
        std::map<std::string, std::map<std::string, Site>> sigs_by_class;
        auto raw_key_for = [](const std::string& mname,
                               const std::vector<std::pair<std::string, std::string>>& params,
                               const std::vector<bool>& param_mutable) {
            std::string key = mname + "(";
            for (size_t i = 0; i < params.size(); i++) {
                key += params[i].first;
                if (i < param_mutable.size() && param_mutable[i]) key += "!mut";
                key += ",";
            }
            key += ")";
            return key;
        };
        auto canonical_key_for = [](const std::string& mname,
                                     const std::vector<std::pair<std::string, std::string>>& params) {
            std::string key = mname + "(";
            for (auto& p : params) key += canonicalType(p.first) + ",";
            key += ")";
            return key;
        };
        auto check_dup = [&](const std::string& cls, const std::string& mname,
                              const std::vector<std::pair<std::string, std::string>>& params,
                              const std::vector<bool>& param_mutable,
                              int file_id, int tok) {
            std::string raw = raw_key_for(mname, params, param_mutable);
            std::string canon = canonical_key_for(mname, params);
            auto& m = sigs_by_class[cls];
            auto [it, inserted] = m.emplace(canon, Site{raw, file_id, tok});
            if (!inserted) {
                std::string msg = (it->second.raw_key == raw)
                    ? "Class '" + cls + "' has a duplicate method '" + mname + "' with the same signature."
                    : "Class '" + cls + "' has a duplicate method '" + mname + "' with the same signature without qualifiers.";
                throw CompileError{file_id, tok, msg}
                    .addNote(it->second.file_id, it->second.tok, "First defined here.");
            }
        };
        for (auto& s : program.slids) {
            for (auto& m : s.methods) {
                if (!m.body) continue;
                check_dup(s.name, m.name, m.params, m.param_mutable, m.file_id, m.tok);
            }
        }
        for (auto& em : program.external_methods) {
            if (!em.body) continue;
            check_dup(em.slid_name, em.method_name, em.params, em.param_mutable, em.file_id, em.tok);
        }
    }
    // const-method mismatch between forward declaration and definition.
    // Covers all four shapes (decl/def in slid.methods or external_methods).
    {
        struct Decl { bool is_const; int file_id; int tok; };
        auto make_key = [](const std::string& mname,
                            const std::vector<std::pair<std::string, std::string>>& params) {
            std::string key = mname + "(";
            for (auto& p : params) key += canonicalType(p.first) + ",";
            key += ")";
            return key;
        };
        std::map<std::string, std::map<std::string, Decl>> decls_by_class;
        for (auto& s : program.slids)
            for (auto& m : s.methods)
                if (!m.body)
                    decls_by_class[s.name].emplace(make_key(m.name, m.params),
                        Decl{m.is_const_method, m.file_id, m.tok});
        for (auto& em : program.external_methods)
            if (!em.body)
                decls_by_class[em.slid_name].emplace(make_key(em.method_name, em.params),
                    Decl{em.is_const_method, em.file_id, em.tok});
        auto check_const = [&](const std::string& cls, const std::string& mname,
                                const std::vector<std::pair<std::string, std::string>>& params,
                                bool is_const, int file_id, int tok) {
            auto cit = decls_by_class.find(cls);
            if (cit == decls_by_class.end()) return;
            auto dit = cit->second.find(make_key(mname, params));
            if (dit == cit->second.end()) return;
            if (dit->second.is_const == is_const) return;
            std::string msg = "Method '" + mname + "' definition is "
                + (is_const ? "const but its declaration is not."
                            : "not const but its declaration is.");
            throw CompileError{file_id, tok, msg}
                .addNote(dit->second.file_id, dit->second.tok, "Declared here.");
        };
        for (auto& s : program.slids)
            for (auto& m : s.methods)
                if (m.body)
                    check_const(s.name, m.name, m.params, m.is_const_method, m.file_id, m.tok);
        for (auto& em : program.external_methods)
            if (em.body)
                check_const(em.slid_name, em.method_name, em.params, em.is_const_method, em.file_id, em.tok);
    }
    // Method decl/def `mutable` agreement. Same canonical signature but
    // differing per-slot mutable bits means decl and def disagree.
    {
        struct Site { std::string raw_key; int file_id; int tok; };
        auto raw_key = [](const std::string& mname,
                          const std::vector<std::pair<std::string, std::string>>& params,
                          const std::vector<bool>& pm) {
            std::string key = mname + "(";
            for (size_t i = 0; i < params.size(); i++) {
                key += params[i].first;
                if (i < pm.size() && pm[i]) key += "!mut";
                key += ",";
            }
            key += ")";
            return key;
        };
        auto canon_key = [](const std::string& mname,
                            const std::vector<std::pair<std::string, std::string>>& params) {
            std::string key = mname + "(";
            for (auto& p : params) key += canonicalType(p.first) + ",";
            key += ")";
            return key;
        };
        std::map<std::string, std::map<std::string, Site>> sigs_by_class;
        auto check = [&](const std::string& cls, const std::string& mname,
                         const std::vector<std::pair<std::string, std::string>>& params,
                         const std::vector<bool>& pm, int file_id, int tok) {
            std::string raw = raw_key(mname, params, pm);
            std::string canon = canon_key(mname, params);
            auto& m = sigs_by_class[cls];
            auto [it, inserted] = m.emplace(canon, Site{raw, file_id, tok});
            if (!inserted && it->second.raw_key != raw) {
                throw CompileError{file_id, tok,
                    "Method '" + cls + ":" + mname +
                    "' declaration and definition disagree on 'mutable' annotation."}
                    .addNote(it->second.file_id, it->second.tok, "First declared here.");
            }
        };
        for (auto& s : program.slids)
            for (auto& m : s.methods)
                check(s.name, m.name, m.params, m.param_mutable, m.file_id, m.tok);
        for (auto& em : program.external_methods)
            check(em.slid_name, em.method_name, em.params, em.param_mutable, em.file_id, em.tok);
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
                    throw CompileError{-1, 0, std::string("Derived field '" + f.name
                        + "' in class '" + s.name + "' shadows a method inherited from '" + s.base_name + "'.")};
                }
            }
            for (auto& m : s.methods) {
                if (base_fields.count(m.name)) {
                    throw CompileError{-1, 0, std::string("Derived method '" + m.name
                        + "' in class '" + s.name + "' shadows a field inherited from '" + s.base_name + "'.")};
                }
            }
        }
    }
}

std::unique_ptr<SwitchStmt> Parser::parseSwitchStmt() {
    [[maybe_unused]] int t_start = pos_;
    expect(TokenType::kSwitch, "Expected 'switch'");
    expect(TokenType::kLParen, "Expected '('");
    auto stmt = make<SwitchStmt>(t_start);
    stmt->expr = parseExpr();
    expect(TokenType::kRParen, "Expected ')'");
    expect(TokenType::kLBrace, "Expected '{'");

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        SwitchCase sc;
        if (peek().type == TokenType::kCase) {
            advance();
            // A case value that is a (possibly qualified) name `Ident(:Ident)*`:
            // the `:` separators belong to the name, the final `:` terminates
            // the label. parseExpr() can't see this — the case context makes
            // `:` a terminator — so scan the Ident-run and split it here.
            if (peek().type == TokenType::kIdentifier
                && pos_ + 1 < (int)tokens_.size()
                && tokens_[pos_ + 1].type == TokenType::kColon) {
                std::vector<int> seg;          // token index of each segment
                seg.push_back(pos_);
                int i = pos_;
                while (i + 2 < (int)tokens_.size()
                       && tokens_[i + 1].type == TokenType::kColon
                       && tokens_[i + 2].type == TokenType::kIdentifier) {
                    i += 2;
                    seg.push_back(i);
                }
                // token after the run decides where the value ends: a `:` means
                // the whole run is the value; otherwise the run's last segment
                // is the case body's first token and the value is the rest.
                int after = i + 1;
                int last = (after < (int)tokens_.size()
                            && tokens_[after].type == TokenType::kColon)
                           ? (int)seg.size() - 1
                           : (int)seg.size() - 2;
                std::string path = tokens_[seg[0]].value;
                for (int s = 1; s <= last; s++)
                    path += ":" + tokens_[seg[s]].value;
                sc.value = make<VarExpr>(seg[last], path);
                pos_ = seg[last] + 1;  // leave the terminator ':' for expect()
            } else {
                colon_terminates_expr_++;
                sc.value = parseExpr();
                colon_terminates_expr_--;
            }
            expect(TokenType::kColon, "Expected ':'");
        } else if (peek().type == TokenType::kDefault) {
            advance();
            expect(TokenType::kColon, "Expected ':'");
            sc.value = nullptr; // default
        } else {
            errorHere("Expected 'case' or 'default' in switch.");
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
    expect(TokenType::kRBrace, "Expected '}'");
    if (peek().type == TokenType::kColon) {
        advance();
        stmt->block_label = expect(TokenType::kIdentifier, "Expected label name").value;
        expect(TokenType::kSemicolon, "Expected ';'");
    } else {
        stmt->block_label = "switch";
    }
    return stmt;
}
