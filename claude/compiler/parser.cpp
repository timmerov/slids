#include "parser.h"
#include "lexer.h"
#include <stdexcept>
#include <functional>
#include <fstream>
#include <sstream>
#include <map>

Parser::Parser(std::vector<Token> tokens, std::string source_dir,
               std::vector<std::string> import_paths,
               std::shared_ptr<std::set<std::string>> imported_once)
    : tokens_(std::move(tokens)), pos_(0), source_dir_(std::move(source_dir)),
      import_paths_(std::move(import_paths)),
      imported_once_(imported_once ? std::move(imported_once)
                                   : std::make_shared<std::set<std::string>>()) {}

void Parser::declareVar(const std::string& name) {
    if (!scope_stack_.empty())
        scope_stack_.back().insert(name);
}

bool Parser::isInScope(const std::string& name) const {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it)
        if (it->count(name)) return true;
    return false;
}

Token& Parser::peek() { return tokens_[pos_]; }

Token& Parser::advance() {
    Token& t = tokens_[pos_];
    if (t.type != TokenType::kEof) pos_++;
    return t;
}

Token& Parser::expect(TokenType type, const std::string& msg) {
    if (peek().type != type)
        throw std::runtime_error("Line " + std::to_string(peek().line)
            + ": " + msg + ", got '" + peek().value + "'");
    return advance();
}

bool Parser::isTypeName(const Token& t) const {
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
    return t.type == TokenType::kIdentifier;
}

bool Parser::isVarDeclLookahead() const {
    // pos_ is at an identifier that might be a type name.
    // scan past it (plus optional template args and pointer/iterator suffix)
    // and check that an identifier (the variable name) follows.
    int i = pos_ + 1; // skip base type name
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

bool Parser::isTemplateCallLookahead() const {
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
    // pos_ is at identifier; pos_+1 is '<'; scan type args and require '>' followed by ';'
    int i = pos_ + 2;
    while (i < (int)tokens_.size()) {
        const Token& ti = tokens_[i];
        if (ti.type == TokenType::kGt)
            return (i + 1 < (int)tokens_.size()) && tokens_[i + 1].type == TokenType::kSemicolon;
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

bool Parser::isTemplateTypeArgLookahead() const {
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
    std::string base;
    if (isTypeName(peek())) base = advance().value;
    else if (isUserTypeName(peek())) base = advance().value;
    else throw std::runtime_error("Line " + std::to_string(peek().line)
        + ": expected type name, got '" + peek().value + "'");
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
    Token t = peek();
    if (t.type == TokenType::kIntLiteral) {
        advance();
        return std::make_unique<IntLiteralExpr>(std::stoll(t.value));
    }
    if (t.type == TokenType::kUintLiteral) {
        advance();
        uint64_t uval = std::stoull(t.value);
        return std::make_unique<IntLiteralExpr>(static_cast<int64_t>(uval), false, true);
    }
    if (t.type == TokenType::kCharLiteral) {
        advance();
        return std::make_unique<IntLiteralExpr>(static_cast<int>(std::stoll(t.value)), /*is_char=*/true);
    }
    if (t.type == TokenType::kFloatLiteral) {
        advance();
        return std::make_unique<FloatLiteralExpr>(std::stod(t.value));
    }
    // type conversion: primitive_type(expr) — legacy form
    if (isTypeName(t) && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kLParen) {
        std::string type_name = advance().value; // consume type keyword
        advance(); // consume '('
        auto operand = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        return std::make_unique<TypeConvExpr>(type_name, std::move(operand));
    }
    // chainable type conversion without outer parens: int=expr, float32=expr, etc.
    // type keywords can't be variable names, so this is unambiguous in expression position
    if (isTypeName(t) && pos_ + 1 < (int)tokens_.size()
        && tokens_[pos_ + 1].type == TokenType::kEquals) {
        std::string type_name = advance().value; // consume type keyword
        advance(); // consume '='
        auto operand = parseExpr();
        return std::make_unique<TypeConvExpr>(type_name, std::move(operand));
    }
    if (t.type == TokenType::kStringLiteral) {
        advance();
        std::string value = t.value;
        while (peek().type == TokenType::kStringLiteral)
            value += advance().value;
        return std::make_unique<StringLiteralExpr>(value);
    }
    if (t.type == TokenType::kTrue)  { advance(); return std::make_unique<IntLiteralExpr>(1); }
    if (t.type == TokenType::kFalse) { advance(); return std::make_unique<IntLiteralExpr>(0); }
    if (t.type == TokenType::kNullptr) { advance(); return std::make_unique<NullptrExpr>(); }
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
            return std::make_unique<PlacementNewExpr>(std::move(addr), elem_type, std::move(args));
        }
        std::string elem_type = parseTypeName();
        if (peek().type == TokenType::kLBracket) {
            advance();
            auto count = parseExpr();
            expect(TokenType::kRBracket, "expected ']'");
            return std::make_unique<NewExpr>(elem_type, std::move(count));
        } else if (peek().type == TokenType::kLParen) {
            advance();
            std::vector<std::unique_ptr<Expr>> args;
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            return std::make_unique<NewScalarExpr>(elem_type, std::move(args));
        } else {
            // new Type; — equivalent to new Type()
            return std::make_unique<NewScalarExpr>(elem_type, std::vector<std::unique_ptr<Expr>>{});
        }
    }
    if (t.type == TokenType::kSizeof) {
        advance();
        expect(TokenType::kLParen, "expected '(' after sizeof");
        auto se = std::make_unique<SizeofExpr>();
        // type form: sizeof(TypeName) — type keyword or uppercase-initial identifier
        auto isSizeofTypeName = [&](const Token& t) {
            if (isTypeName(t)) return true;
            return t.type == TokenType::kIdentifier && !t.value.empty()
                   && std::isupper((unsigned char)t.value[0]);
        };
        if (isSizeofTypeName(peek())) {
            se->type_name = parseTypeName();
        } else {
            se->operand = parseExpr();
        }
        expect(TokenType::kRParen, "expected ')'");
        return se;
    }
    if (t.type == TokenType::kIdentifier) {
        advance();
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
            auto call = std::make_unique<CallExpr>(t.value, std::move(args));
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
            return std::make_unique<CallExpr>(t.value, std::move(args));
        }
        return std::make_unique<VarExpr>(t.value);
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
            return std::make_unique<TypeConvExpr>(type_name, std::move(operand));
        }
        pos_ = saved; // not a type conversion — backtrack and fall through
    }
    if (t.type == TokenType::kLParen) {
        advance();
        auto first = parseExpr();
        if (peek().type == TokenType::kComma) {
            auto tuple = std::make_unique<TupleExpr>();
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
    throw std::runtime_error("Line " + std::to_string(t.line)
        + ": expected expression, got '" + t.value + "'");
}

// postfix: handle .field and .method(args) chaining
std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> base) {
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
                base = std::make_unique<MethodCallExpr>(std::move(base), member, std::move(args));
            } else {
                base = std::make_unique<FieldAccessExpr>(std::move(base), member);
            }
        } else if (peek().type == TokenType::kLBracket) {
            advance();
            auto idx = parseExpr();
            if (peek().type == TokenType::kDotDot) {
                advance();
                auto end_expr = parseExpr();
                expect(TokenType::kRBracket, "expected ']'");
                base = std::make_unique<SliceExpr>(std::move(base), std::move(idx), std::move(end_expr));
            } else {
                expect(TokenType::kRBracket, "expected ']'");
                base = std::make_unique<ArrayIndexExpr>(std::move(base), std::move(idx));
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
            base = std::make_unique<DerefExpr>(std::move(base));
        } else if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
            std::string op = (peek().type == TokenType::kPlusPlus) ? "++" : "--";
            advance();
            // ptr++^ — post-inc/dec then deref: treat as PostIncDerefExpr
            if (peek().type == TokenType::kBitXor) {
                advance();
                base = std::make_unique<PostIncDerefExpr>(std::move(base), op);
            } else {
                std::string uop = (op == "++") ? "post++" : "post--";
                base = std::make_unique<UnaryExpr>(uop, std::move(base));
            }
        } else {
            break;
        }
    }
    return base;
}

std::unique_ptr<Expr> Parser::parseUnary() {
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
                    return std::make_unique<PtrCastExpr>(target, std::move(operand));
                }
            } catch (...) {}
        }
        pos_ = saved; // not a cast — restore for comparison operator
    }
    if (peek().type == TokenType::kPlus) {
        advance();
        return parsePrimary();
    }
    if (peek().type == TokenType::kMinus) {
        advance();
        return std::make_unique<BinaryExpr>("-",
            std::make_unique<IntLiteralExpr>(0), parsePrimary());
    }
    if (peek().type == TokenType::kNot) {
        advance();
        return std::make_unique<UnaryExpr>("!", parseUnary());
    }
    if (peek().type == TokenType::kBitNot) {
        advance();
        return std::make_unique<UnaryExpr>("~", parseUnary());
    }
    // pre-increment/decrement: ++x, --x, ++(ptr^) — returns new value
    if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
        std::string op = (peek().type == TokenType::kPlusPlus) ? "pre++" : "pre--";
        advance();
        auto operand = parsePostfix(parsePrimary());  // handles ++(ref^), ++arr[i], etc.
        return std::make_unique<UnaryExpr>(op, std::move(operand));
    }
    // prefix ^ — take address: ^x, ^arr[i][j]
    if (peek().type == TokenType::kBitXor) {
        advance();
        auto operand = parsePostfix(parsePrimary());
        return std::make_unique<AddrOfExpr>(std::move(operand));
    }
    return parsePostfix(parsePrimary());
}

std::unique_ptr<Expr> Parser::parseMulDiv() {
    auto left = parseUnary();
    while (peek().type == TokenType::kStar  ||
           peek().type == TokenType::kSlash  ||
           peek().type == TokenType::kPercent) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseUnary());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAddSub() {
    auto left = parseMulDiv();
    while (peek().type == TokenType::kPlus ||
           peek().type == TokenType::kMinus) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseMulDiv());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseShift() {
    auto left = parseAddSub();
    while (peek().type == TokenType::kLShift ||
           peek().type == TokenType::kRShift) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseAddSub());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseRelational() {
    auto left = parseShift();
    while (peek().type == TokenType::kLt   ||
           peek().type == TokenType::kGt   ||
           peek().type == TokenType::kLtEq ||
           peek().type == TokenType::kGtEq) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseShift());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    auto left = parseRelational();
    while (peek().type == TokenType::kEqEq ||
           peek().type == TokenType::kNotEq) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseRelational());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitAnd() {
    auto left = parseEquality();
    while (peek().type == TokenType::kBitAnd) {
        advance();
        left = std::make_unique<BinaryExpr>("&", std::move(left), parseEquality());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitXor() {
    auto left = parseBitAnd();
    while (peek().type == TokenType::kBitXor) {
        advance();
        left = std::make_unique<BinaryExpr>("^", std::move(left), parseBitAnd());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitOr() {
    auto left = parseBitXor();
    while (peek().type == TokenType::kBitOr) {
        advance();
        left = std::make_unique<BinaryExpr>("|", std::move(left), parseBitXor());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto left = parseBitOr();
    while (peek().type == TokenType::kAnd) {
        advance();
        left = std::make_unique<BinaryExpr>("&&", std::move(left), parseBitOr());
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseExpr() {
    auto left = parseLogicalAnd();
    while (peek().type == TokenType::kOr ||
           peek().type == TokenType::kXorXor) {
        std::string op = advance().value;
        left = std::make_unique<BinaryExpr>(op, std::move(left), parseLogicalAnd());
    }
    return left;
}

// --- Statement parsing ---

std::unique_ptr<BlockStmt> Parser::parseBlock(std::vector<std::string> predeclare) {
    expect(TokenType::kLBrace, "expected '{'");
    scope_stack_.push_back({});
    for (auto& n : predeclare)
        scope_stack_.back().insert(n);
    auto block = std::make_unique<BlockStmt>();
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof)
        block->stmts.push_back(parseStmt());
    scope_stack_.pop_back();
    expect(TokenType::kRBrace, "expected '}'");
    return block;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    Token t = peek();

    if (t.type == TokenType::kDelete) {
        advance();
        auto operand = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<DeleteStmt>(std::move(operand));
    }

    if (t.type == TokenType::kReturn) {
        advance();
        if (peek().type == TokenType::kSemicolon) {
            advance();
            return std::make_unique<ReturnStmt>(nullptr);
        }
        auto expr = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<ReturnStmt>(std::move(expr));
    }

    if (t.type == TokenType::kBreak) {
        advance();
        auto stmt = std::make_unique<BreakStmt>();
        if (peek().type == TokenType::kIntLiteral) {
            stmt->number = std::stoi(advance().value);
        } else if (peek().type == TokenType::kIdentifier) {
            stmt->label = advance().value;
        }
        expect(TokenType::kSemicolon, "expected ';'");
        return stmt;
    }

    if (t.type == TokenType::kContinue) {
        advance();
        auto stmt = std::make_unique<ContinueStmt>();
        if (peek().type == TokenType::kIntLiteral) {
            stmt->number = std::stoi(advance().value);
        } else if (peek().type == TokenType::kIdentifier) {
            stmt->label = advance().value;
        }
        expect(TokenType::kSemicolon, "expected ';'");
        return stmt;
    }

    // pre-increment/decrement statement: ++x;  ++ref^;  ++(ref^);
    if (t.type == TokenType::kPlusPlus || t.type == TokenType::kMinusMinus) {
        std::string op = (t.type == TokenType::kPlusPlus) ? "pre++" : "pre--";
        advance();
        auto operand = parsePostfix(parsePrimary());
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<ExprStmt>(
            std::make_unique<UnaryExpr>(op, std::move(operand)));
    }

    // naked block: { stmt; stmt; ... }
    if (t.type == TokenType::kLBrace) {
        return parseBlock();
    }

    if (t.type == TokenType::kIf) {
        advance();
        auto stmt = std::make_unique<IfStmt>();
        expect(TokenType::kLParen, "expected '('");
        stmt->cond = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        stmt->then_block = parseBlock();
        if (peek().type == TokenType::kElse) {
            advance();
            if (peek().type == TokenType::kIf) {
                auto else_block = std::make_unique<BlockStmt>();
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
        auto stmt = std::make_unique<WhileStmt>();
        if (peek().type == TokenType::kLBrace) {
            // bottom-condition while: while { body } :label (cond);
            stmt->bottom_condition = true;
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
            }
            expect(TokenType::kLParen, "expected '('");
            stmt->cond = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
        } else {
            expect(TokenType::kLParen, "expected '('");
            if (peek().type == TokenType::kRParen)
                stmt->cond = std::make_unique<IntLiteralExpr>(1);  // while () == while (true)
            else
                stmt->cond = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
            }
        }
        return stmt;
    }

    if (t.type == TokenType::kSwitch) {
        return parseSwitchStmt();
    }

    if (t.type == TokenType::kFor) {
        advance();
        // for EnumType var in EnumType { ... }  — enum iteration
        // detected by: user type name, identifier, 'in', user type name (no '(')
        if (isUserTypeName(peek())) {
            int saved = pos_;
            std::string var_type = advance().value;   // e.g. "Piece"
            if (peek().type == TokenType::kIdentifier) {
                std::string var_name = advance().value;
                if (peek().type == TokenType::kIn) {
                    advance(); // consume 'in'
                    if (isUserTypeName(peek())) {
                        auto stmt = std::make_unique<ForEnumStmt>();
                        stmt->var_type = var_type;
                        stmt->var_name = var_name;
                        stmt->enum_name = advance().value;
                        stmt->body = parseBlock();
                        if (peek().type == TokenType::kColon) {
                            advance();
                            stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
                        }
                        return stmt;
                    }
                }
            }
            // not an enum-for — backtrack and fall through to range for
            pos_ = saved;
        }
        // type is optional — "for int i in" vs "for i in" (reuse existing var)
        std::string for_var_type, for_var_name;
        if (isTypeName(peek())) {
            for_var_type = parseTypeName();
            for_var_name = expect(TokenType::kIdentifier, "expected variable name").value;
        } else if (peek().type == TokenType::kIdentifier) {
            for_var_type = ""; // reuse existing variable
            for_var_name = advance().value;
        } else {
            throw std::runtime_error("Line " + std::to_string(peek().line)
                + ": expected variable name in for loop");
        }
        expect(TokenType::kIn, "expected 'in'");
        if (peek().type == TokenType::kStringLiteral || peek().type == TokenType::kIdentifier) {
            auto stmt = std::make_unique<ForArrayStmt>();
            stmt->var_name = for_var_name;
            stmt->array_expr = parseExpr();
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
            }
            return stmt;
        }
        expect(TokenType::kLParen, "expected '('");
        auto first_expr = parseExpr();
        if (peek().type == TokenType::kDotDot) {
            // for var in (start..end) — numeric range
            advance(); // consume '..'
            auto stmt = std::make_unique<ForRangeStmt>();
            stmt->var_type = for_var_type;
            stmt->var_name = for_var_name;
            stmt->range_start = std::move(first_expr);
            stmt->range_end = parseExpr();
            expect(TokenType::kRParen, "expected ')'");
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
            }
            return stmt;
        } else {
            // for var in (expr, expr, ...) — tuple iteration
            auto stmt = std::make_unique<ForTupleStmt>();
            stmt->var_name = for_var_name;
            stmt->elements.push_back(std::move(first_expr));
            while (peek().type == TokenType::kComma) {
                advance(); // consume ','
                stmt->elements.push_back(parseExpr());
            }
            expect(TokenType::kRParen, "expected ')'");
            stmt->body = parseBlock();
            if (peek().type == TokenType::kColon) {
                advance();
                stmt->block_label = expect(TokenType::kIdentifier, "expected label name").value;
            }
            return stmt;
        }
    }

    // tuple return nested function or tuple destructure: starts with '('
    if (t.type == TokenType::kLParen) {
        // scan to matching ')' and check what follows
        int depth = 1, scan = pos_ + 1;
        while (scan < (int)tokens_.size() && depth > 0) {
            if (tokens_[scan].type == TokenType::kLParen) depth++;
            else if (tokens_[scan].type == TokenType::kRParen) depth--;
            scan++;
        }
        // scan is now at the token after the matching ')'
        if (scan < (int)tokens_.size()
            && tokens_[scan].type == TokenType::kIdentifier
            && scan + 1 < (int)tokens_.size()
            && tokens_[scan + 1].type == TokenType::kLParen) {
            // (type name, ...) funcName() { ... } — nested function with tuple return
            auto stmt = std::make_unique<NestedFunctionDefStmt>();
            stmt->def = parseNestedFunctionDef();
            return stmt;
        } else if (scan < (int)tokens_.size()
                   && tokens_[scan].type == TokenType::kEquals) {
            // (type name, ...) = expr; — tuple destructure
            // slots may be: empty (skip), bare name (infer type), or type + name (declared).
            advance(); // consume '('
            auto td = std::make_unique<TupleDestructureStmt>();
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
        } else if (scan < (int)tokens_.size()
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
            std::string name = expect(TokenType::kIdentifier, "expected variable name").value;
            if (peek().type == TokenType::kSemicolon) {
                advance();
                declareVar(name);
                return std::make_unique<VarDeclStmt>(type, name, nullptr);
            }
            if (peek().type == TokenType::kArrowLeft) {
                advance();
                auto init = parseExpr();
                expect(TokenType::kSemicolon, "expected ';'");
                declareVar(name);
                return std::make_unique<VarDeclStmt>(type, name, std::move(init),
                                                      std::vector<std::unique_ptr<Expr>>{}, true);
            }
            expect(TokenType::kEquals, "expected '='");
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, std::move(init));
        }
        // fall through to expression statement
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
                    auto stmt = std::make_unique<NestedFunctionDefStmt>();
                    stmt->def = parseNestedFunctionDef();
                    return stmt;
                }
            }
        }
        // not a nested function — fall through to variable declaration
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected variable name").value;

        // array declaration: Type name[d0][d1...] = ((..),(..),..);
        if (peek().type == TokenType::kLBracket) {
            auto arr = std::make_unique<ArrayDeclStmt>();
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
            declareVar(arr->name);
            return arr;
        }

        if (peek().type == TokenType::kArrowLeft) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, std::move(init), std::vector<std::unique_ptr<Expr>>{}, true);
        }
        if (peek().type == TokenType::kSemicolon) {
            advance();
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, nullptr);
        }
        expect(TokenType::kEquals, "expected '='");
        auto init = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        declareVar(name);
        return std::make_unique<VarDeclStmt>(type, name, std::move(init));
    }

    // user-defined type variable declaration: counter c; or counter c(5); or piece board[8][8] = ...
    if (t.type == TokenType::kIdentifier && isVarDeclLookahead()) {
        std::string type = parseTypeName(); // consumes Name plus any trailing ^ or []
        std::string name = expect(TokenType::kIdentifier, "expected variable name").value;

        // pointer or reference variable with initializer: Type[] ptr = expr  or  Type^ ref = expr
        if (peek().type == TokenType::kEquals) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, std::move(init));
        }
        if (peek().type == TokenType::kArrowLeft) {
            advance();
            auto init = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            declareVar(name);
            return std::make_unique<VarDeclStmt>(type, name, std::move(init), std::vector<std::unique_ptr<Expr>>{}, true);
        }

        // array declaration: Type name[d0][d1] = (...)
        if (peek().type == TokenType::kLBracket) {
            auto arr = std::make_unique<ArrayDeclStmt>();
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
            declareVar(arr->name);
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
        declareVar(name);
        return std::make_unique<VarDeclStmt>(type, name, std::move(init), std::move(ctor_args));
    }

    // identifier — assignment, compound assignment, method call, or function call
    if (t.type == TokenType::kIdentifier) {
        std::string name = t.value;
        advance();

        // deref assignment: ptr^ = val  or  ptr^.field = val  or  ptr^.method()
        if (peek().type == TokenType::kBitXor) {
            // parse the full postfix chain starting from deref
            auto base = parsePostfix(std::make_unique<VarExpr>(name));
            // base is now DerefExpr, or DerefExpr.field, or DerefExpr.method(...)

            if (peek().type == TokenType::kEquals) {
                advance();
                auto val = parseExpr();
                expect(TokenType::kSemicolon, "expected ';'");
                // ptr^ = val
                if (auto* de = dynamic_cast<DerefExpr*>(base.get())) {
                    return std::make_unique<DerefAssignStmt>(std::move(de->operand), std::move(val));
                }
                // ptr^.field = val
                if (auto* fa = dynamic_cast<FieldAccessExpr*>(base.get())) {
                    auto obj = std::move(fa->object);
                    std::string field = fa->field;
                    return std::make_unique<FieldAssignStmt>(std::move(obj), field, std::move(val));
                }
                throw std::runtime_error("invalid deref assignment target");
            }
            // ptr^ += rhs  (compound assignment through dereference)
            {
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
                    if (auto* de = dynamic_cast<DerefExpr*>(base.get())) {
                        auto* ve = dynamic_cast<VarExpr*>(de->operand.get());
                        if (!ve) throw std::runtime_error("compound deref-assign requires simple pointer variable");
                        std::string ptr_name = ve->name;
                        advance();
                        auto rhs = parseExpr();
                        expect(TokenType::kSemicolon, "expected ';'");
                        // desugar: ptr^ op= rhs  ->  DerefAssignStmt(ptr, BinaryExpr(op, DerefExpr(ptr), rhs))
                        auto lhs_read = std::make_unique<DerefExpr>(std::make_unique<VarExpr>(ptr_name));
                        auto bin = std::make_unique<BinaryExpr>(cop->second, std::move(lhs_read), std::move(rhs));
                        return std::make_unique<DerefAssignStmt>(std::make_unique<VarExpr>(ptr_name), std::move(bin));
                    }
                    throw std::runtime_error("compound assignment requires a dereference target");
                }
            }
            // ptr^++  or  ptr^--  as a statement — ExprStmt routes to the correct codegen
            if (auto* ue = dynamic_cast<UnaryExpr*>(base.get())) {
                if (ue->op == "post++" || ue->op == "post--") {
                    expect(TokenType::kSemicolon, "expected ';'");
                    return std::make_unique<ExprStmt>(std::move(base));
                }
            }
            // ptr^.method()
            if (auto* mc = dynamic_cast<MethodCallExpr*>(base.get())) {
                expect(TokenType::kSemicolon, "expected ';'");
                return std::make_unique<MethodCallStmt>(
                    std::move(mc->object), mc->method, std::move(mc->args));
            }
            throw std::runtime_error("Line " + std::to_string(peek().line)
                + ": unexpected token after dereference");
        }

        // method call or field access chain followed by assignment
        if (peek().type == TokenType::kDot) {
            // parse the chain as an expression starting from VarExpr
            auto base = parsePostfix(std::make_unique<VarExpr>(name));

            // field assignment: obj.field_ = expr;
            if (peek().type == TokenType::kEquals) {
                advance();
                auto val = parseExpr();
                expect(TokenType::kSemicolon, "expected ';'");
                // unwrap to FieldAccessExpr
                if (auto* fa = dynamic_cast<FieldAccessExpr*>(base.get())) {
                    auto obj = std::move(fa->object);
                    std::string field = fa->field;
                    return std::make_unique<FieldAssignStmt>(std::move(obj), field, std::move(val));
                }
                // unwrap obj.field[idx] = val -> IndexAssignStmt
                if (auto* ai = dynamic_cast<ArrayIndexExpr*>(base.get())) {
                    auto b = std::move(ai->base);
                    auto idx = std::move(ai->index);
                    return std::make_unique<IndexAssignStmt>(std::move(b), std::move(idx), std::move(val));
                }
                throw std::runtime_error("invalid assignment target");
            }

            // method call statement
            if (auto* mc = dynamic_cast<MethodCallExpr*>(base.get())) {
                expect(TokenType::kSemicolon, "expected ';'");
                return std::make_unique<MethodCallStmt>(
                    std::move(mc->object), mc->method, std::move(mc->args));
            }
            throw std::runtime_error("Line " + std::to_string(peek().line)
                + ": expected ';' after method call");
        }

        // post-increment/decrement statement: x++; x--; or ptr++^ = val;
        if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
            std::string op = (peek().type == TokenType::kPlusPlus) ? "++" : "--";
            advance();
            // ptr++^ = val  — post-inc-deref lvalue
            if (peek().type == TokenType::kBitXor) {
                advance();
                if (peek().type == TokenType::kEquals) {
                    advance();
                    auto val = parseExpr();
                    expect(TokenType::kSemicolon, "expected ';'");
                    return std::make_unique<PostIncDerefAssignStmt>(
                        std::make_unique<VarExpr>(name), op, std::move(val));
                }
                if (peek().type == TokenType::kArrowBoth) {
                    advance();
                    auto lhs = std::make_unique<PostIncDerefExpr>(std::make_unique<VarExpr>(name), op);
                    auto rhs = parseExpr();
                    expect(TokenType::kSemicolon, "expected ';'");
                    return std::make_unique<SwapStmt>(std::move(lhs), std::move(rhs));
                }
                // ptr++^ as expression statement (rvalue use)
                expect(TokenType::kSemicolon, "expected ';'");
                return std::make_unique<ExprStmt>(
                    std::make_unique<PostIncDerefExpr>(std::make_unique<VarExpr>(name), op));
            }
            std::string uop = (op == "++") ? "post++" : "post--";
            expect(TokenType::kSemicolon, "expected ';'");
            return std::make_unique<ExprStmt>(
                std::make_unique<UnaryExpr>(uop, std::make_unique<VarExpr>(name)));
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
            auto stmt = std::make_unique<CallStmt>(name, std::move(args));
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
            return std::make_unique<CallStmt>(name, std::move(args));
        }

        // plain assignment — or inferred declaration if name is not yet in scope and not a class field
        if (peek().type == TokenType::kEquals) {
            advance();
            auto value = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            if (!isInScope(name) && !current_slid_fields_.count(name) && name != "self") {
                declareVar(name);
                return std::make_unique<VarDeclStmt>("", name, std::move(value));
            }
            return std::make_unique<AssignStmt>(name, std::move(value));
        }
        // move assignment — or inferred move-init if name is not yet in scope and not a class field
        if (peek().type == TokenType::kArrowLeft) {
            advance();
            auto value = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            if (!isInScope(name) && !current_slid_fields_.count(name) && name != "self") {
                declareVar(name);
                return std::make_unique<VarDeclStmt>("", name, std::move(value),
                                                      std::vector<std::unique_ptr<Expr>>{}, true);
            }
            return std::make_unique<AssignStmt>(name, std::move(value), true);
        }

        // compound assignment
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
        auto it = compound_ops.find(peek().type);
        if (it != compound_ops.end()) {
            advance();
            auto rhs = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            auto lhs = std::make_unique<VarExpr>(name);
            auto value = std::make_unique<BinaryExpr>(it->second, std::move(lhs), std::move(rhs));
            return std::make_unique<AssignStmt>(name, std::move(value));
        }

        // indexed statement: name[expr]... — may be simple assign, method call on element, etc.
        if (peek().type == TokenType::kLBracket) {
            advance(); // consume '['
            auto idx = parseExpr();
            expect(TokenType::kRBracket, "expected ']'");
            // simple index assignment (copy or move)
            if (peek().type == TokenType::kEquals || peek().type == TokenType::kArrowLeft) {
                bool is_move = (peek().type == TokenType::kArrowLeft);
                advance();
                auto val = parseExpr();
                expect(TokenType::kSemicolon, "expected ';'");
                return std::make_unique<IndexAssignStmt>(
                    std::make_unique<VarExpr>(name), std::move(idx), std::move(val), is_move);
            }
            // chained: name[idx].field = val, name[idx].method(), name[idx][j]..., etc.
            auto base = parsePostfix(std::make_unique<ArrayIndexExpr>(
                std::make_unique<VarExpr>(name), std::move(idx)));
            if (peek().type == TokenType::kEquals || peek().type == TokenType::kArrowLeft) {
                bool is_move = (peek().type == TokenType::kArrowLeft);
                advance();
                auto val = parseExpr();
                expect(TokenType::kSemicolon, "expected ';'");
                if (auto* fa = dynamic_cast<FieldAccessExpr*>(base.get())) {
                    auto obj = std::move(fa->object);
                    std::string field = fa->field;
                    return std::make_unique<FieldAssignStmt>(std::move(obj), field, std::move(val), is_move);
                }
                if (auto* ai = dynamic_cast<ArrayIndexExpr*>(base.get())) {
                    auto b = std::move(ai->base);
                    auto ix = std::move(ai->index);
                    return std::make_unique<IndexAssignStmt>(std::move(b), std::move(ix), std::move(val), is_move);
                }
                throw std::runtime_error("invalid indexed assignment target");
            }
            if (auto* mc = dynamic_cast<MethodCallExpr*>(base.get())) {
                expect(TokenType::kSemicolon, "expected ';'");
                return std::make_unique<MethodCallStmt>(
                    std::move(mc->object), mc->method, std::move(mc->args));
            }
            expect(TokenType::kSemicolon, "expected ';'");
            return std::make_unique<ExprStmt>(std::move(base));
        }

        throw std::runtime_error("Line " + std::to_string(peek().line)
            + ": expected '(' or '=' after '" + name + "'");
    }

    throw std::runtime_error("Line " + std::to_string(t.line)
        + ": unexpected token '" + t.value + "'");
}

// --- Top-level parsing ---

NestedFunctionDef Parser::parseNestedFunctionDef() {
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
    fn.name = expect(TokenType::kIdentifier, "expected function name").value;
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        fn.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    std::vector<std::string> param_names;
    for (auto& p : fn.params) param_names.push_back(p.second);
    fn.body = parseBlock(param_names);
    return fn;
}

MethodDef Parser::parseMethodDef() {
    MethodDef m;
    // new syntax: op overloads have no explicit return type (implied void / self)
    if (peek().type == TokenType::kIdentifier && peek().value == "op") {
        m.return_type = "void";
    } else {
        m.return_type = parseTypeName();
    }
    m.name = expect(TokenType::kIdentifier, "expected method name").value;
    if (m.name == "op" && peek().type == TokenType::kEquals)     { advance(); m.name = "op="; }
    else if (m.name == "op" && peek().type == TokenType::kArrowLeft) { advance(); m.name = "op<-"; }
    else if (m.name == "op" && peek().type == TokenType::kArrowBoth) { advance(); m.name = "op<->"; }
    else if (m.name == "op" && peek().type == TokenType::kPlus)    { advance(); m.name = "op+"; }
    else if (m.name == "op" && peek().type == TokenType::kMinus)   { advance(); m.name = "op-"; }
    else if (m.name == "op" && peek().type == TokenType::kStar)    { advance(); m.name = "op*"; }
    else if (m.name == "op" && peek().type == TokenType::kSlash)   { advance(); m.name = "op/"; }
    else if (m.name == "op" && peek().type == TokenType::kPlusEq)  { advance(); m.name = "op+="; }
    else if (m.name == "op" && peek().type == TokenType::kMinusEq) { advance(); m.name = "op-="; }
    else if (m.name == "op" && peek().type == TokenType::kStarEq)  { advance(); m.name = "op*="; }
    else if (m.name == "op" && peek().type == TokenType::kSlashEq) { advance(); m.name = "op/="; }
    else if (m.name == "op" && peek().type == TokenType::kBracketAssign) { advance(); m.name = "op[]="; }
    else if (m.name == "op" && peek().type == TokenType::kLBracket) {
        advance();
        expect(TokenType::kRBracket, "expected ']'");
        m.name = "op[]";
    }
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        m.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    if (peek().type == TokenType::kSemicolon) {
        advance(); // forward declaration — no body
    } else {
        std::vector<std::string> param_names;
        for (auto& p : m.params) param_names.push_back(p.second);
        m.body = parseBlock(param_names);
    }
    return m;
}

SlidDef Parser::parseSlidDef() {
    SlidDef slid;
    slid.name = peek().value;
    advance(); // consume class name

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
    // ... at start = has_ellipsis_prefix (implementation of incomplete class)
    // ... at end   = has_ellipsis_suffix (declaration of incomplete class)
    expect(TokenType::kLParen, "expected '('");

    // leading ellipsis?
    if (peek().type == TokenType::kEllipsis) {
        advance(); // consume ...
        if (peek().type == TokenType::kComma) advance(); // consume , before fields
        // lone (...) means all fields are private — treat as suffix (declaration case)
        // (..., field) means this is the implementation of an incomplete class
        if (peek().type == TokenType::kRParen)
            slid.has_ellipsis_suffix = true;
        else {
            slid.has_ellipsis_prefix = true;
            slid.is_transport_impl = true;  // emits __pinit and __sizeof for the consumer
        }
    }

    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        // trailing ellipsis?
        if (peek().type == TokenType::kEllipsis) {
            slid.has_ellipsis_suffix = true;
            advance(); // consume ...
            if (peek().type == TokenType::kComma) advance();
            break; // ... must be last
        }
        FieldDef f;
        f.type = parseTypeName();
        f.name = expect(TokenType::kIdentifier, "expected field name").value;
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

    // record field names to prevent method-body field assignments from being mistaken for inferred declarations
    current_slid_fields_.clear();
    for (auto& f : slid.fields) current_slid_fields_.insert(f.name);
    all_slid_fields_[slid.name] = current_slid_fields_;

    // parse body: methods and optional constructor code
    expect(TokenType::kLBrace, "expected '{'");

    auto ctor_body = std::make_unique<BlockStmt>();
    bool has_ctor_code = false;

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        // explicit constructor: _() { ... }  or forward decl: _();
        if (peek().type == TokenType::kIdentifier && peek().value == "_"
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume _
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            if (slid.has_explicit_ctor_decl)
                throw std::runtime_error("Line " + std::to_string(peek().line)
                    + ": constructor already defined in '" + slid.name + "'");
            slid.has_explicit_ctor_decl = true; // declared — consumer must call ctor
            if (peek().type == TokenType::kSemicolon) {
                advance(); // forward declaration only
            } else {
                slid.explicit_ctor_body = parseBlock();
            }
            continue;
        }
        // explicit destructor: ~() { ... }  or forward decl: ~();
        if (peek().type == TokenType::kBitNot
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            advance(); // consume ~
            expect(TokenType::kLParen, "expected '('");
            expect(TokenType::kRParen, "expected ')'");
            if (slid.has_explicit_dtor_decl)
                throw std::runtime_error("Line " + std::to_string(peek().line)
                    + ": destructor already defined in '" + slid.name + "'");
            slid.has_explicit_dtor_decl = true; // declared — consumer must call dtor
            if (peek().type == TokenType::kSemicolon) {
                advance(); // forward declaration only
            } else {
                slid.dtor_body = parseBlock();
            }
            continue;
        }
        // method definition: starts with a type name followed by identifier followed by (
        // also handles operator methods: op=(...)  op<-(...)  op+(...)  etc.
        auto isMethodDecl = [&]() {
            // new syntax: op<symbol>( without explicit return type
            if (peek().type == TokenType::kIdentifier && peek().value == "op") {
                if (pos_ + 1 >= (int)tokens_.size()) return false;
                auto t = tokens_[pos_ + 1].type;
                // op[]= : op + []= + (
                if (t == TokenType::kBracketAssign) {
                    return pos_ + 2 < (int)tokens_.size() && tokens_[pos_ + 2].type == TokenType::kLParen;
                }
                // op[] : op + [ + ] + (
                if (t == TokenType::kLBracket) {
                    return pos_ + 3 < (int)tokens_.size()
                        && tokens_[pos_ + 2].type == TokenType::kRBracket
                        && tokens_[pos_ + 3].type == TokenType::kLParen;
                }
                bool is_op_tok = (t == TokenType::kEquals   || t == TokenType::kArrowLeft
                    || t == TokenType::kArrowBoth || t == TokenType::kPlus   || t == TokenType::kMinus
                    || t == TokenType::kStar      || t == TokenType::kSlash  || t == TokenType::kPlusEq
                    || t == TokenType::kMinusEq   || t == TokenType::kStarEq || t == TokenType::kSlashEq
                    || t == TokenType::kEqEq      || t == TokenType::kNotEq
                    || t == TokenType::kLt        || t == TokenType::kGt
                    || t == TokenType::kLtEq      || t == TokenType::kGtEq);
                if (!is_op_tok) return false;
                if (pos_ + 2 >= (int)tokens_.size()) return false;
                return tokens_[pos_ + 2].type == TokenType::kLParen;
            }
            // regular method: return-type name(
            // return type may include pointer/iterator suffixes: ^ or []
            if (!(isTypeName(peek()) || isUserTypeName(peek()))) return false;
            int name_pos = pos_ + 1;
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
            if (tokens_[name_pos].type != TokenType::kIdentifier) return false;
            if (tokens_[name_pos + 1].type == TokenType::kLParen) return true;
            // int op[]( pattern
            if (tokens_[name_pos].value == "op"
                    && tokens_[name_pos + 1].type == TokenType::kLBracket
                    && name_pos + 3 < (int)tokens_.size()
                    && tokens_[name_pos + 2].type == TokenType::kRBracket
                    && tokens_[name_pos + 3].type == TokenType::kLParen) return true;
            // old op= pattern: void op = (  (kept for backward compatibility)
            if (tokens_[name_pos].value != "op") return false;
            if (name_pos + 2 >= (int)tokens_.size()) return false;
            return (tokens_[name_pos + 1].type == TokenType::kEquals || tokens_[name_pos + 1].type == TokenType::kArrowLeft)
                && tokens_[name_pos + 2].type == TokenType::kLParen;
        };
        if (isMethodDecl()) {
            slid.methods.push_back(parseMethodDef());
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
    return slid;
}

EnumDef Parser::parseEnumDef() {
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
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        em.params.emplace_back(type, name);
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
    // TypeName { [returnType] methodName(params) { body } ... }
    std::string slid_name = expect(TokenType::kIdentifier, "expected class name").value;
    {
        auto fit = all_slid_fields_.find(slid_name);
        current_slid_fields_ = (fit != all_slid_fields_.end()) ? fit->second : std::set<std::string>{};
    }
    expect(TokenType::kLBrace, "expected '{'");
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        ExternalMethodDef em;
        em.slid_name = slid_name;
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
        // dtor: ~() { ... }
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
        if (peek().type == TokenType::kIdentifier && peek().value == "op") {
            em.return_type = "void";
        } else {
            em.return_type = parseTypeName();
        }
        em.method_name = expect(TokenType::kIdentifier, "expected method name").value;
        if (em.method_name == "op" && peek().type == TokenType::kEquals)     { advance(); em.method_name = "op="; }
        else if (em.method_name == "op" && peek().type == TokenType::kArrowLeft) { advance(); em.method_name = "op<-"; }
        else if (em.method_name == "op" && peek().type == TokenType::kArrowBoth) { advance(); em.method_name = "op<->"; }
        else if (em.method_name == "op" && peek().type == TokenType::kPlus)    { advance(); em.method_name = "op+"; }
        else if (em.method_name == "op" && peek().type == TokenType::kMinus)   { advance(); em.method_name = "op-"; }
        else if (em.method_name == "op" && peek().type == TokenType::kStar)    { advance(); em.method_name = "op*"; }
        else if (em.method_name == "op" && peek().type == TokenType::kSlash)   { advance(); em.method_name = "op/"; }
        else if (em.method_name == "op" && peek().type == TokenType::kPlusEq)  { advance(); em.method_name = "op+="; }
        else if (em.method_name == "op" && peek().type == TokenType::kMinusEq) { advance(); em.method_name = "op-="; }
        else if (em.method_name == "op" && peek().type == TokenType::kStarEq)  { advance(); em.method_name = "op*="; }
        else if (em.method_name == "op" && peek().type == TokenType::kSlashEq) { advance(); em.method_name = "op/="; }
        expect(TokenType::kLParen, "expected '('");
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            std::string type = parseTypeName();
            std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
            em.params.emplace_back(type, name);
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
        program.external_methods.push_back(std::move(em));
    }
    expect(TokenType::kRBrace, "expected '}'");
    current_slid_fields_.clear();
}

FunctionDef Parser::parseFunctionDef() {
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
        std::string fname = expect(TokenType::kIdentifier, "expected function name").value;
        if (fname == "op") {
            static const std::map<TokenType, std::string> op_map = {
                {TokenType::kPlus, "+"}, {TokenType::kMinus, "-"},
                {TokenType::kStar, "*"}, {TokenType::kSlash, "/"},
                {TokenType::kEqEq, "=="}, {TokenType::kNotEq, "!="},
                {TokenType::kLt, "<"}, {TokenType::kGt, ">"},
                {TokenType::kLtEq, "<="}, {TokenType::kGtEq, ">="},
            };
            auto it = op_map.find(peek().type);
            if (it != op_map.end()) { advance(); fname = "op" + it->second; }
        }
        fn.name = fname;
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
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        fn.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
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
                throw std::runtime_error("import: cannot find '" + module + ".slh'");

            // import-once: skip if this header has already been loaded in this compile
            if (!imported_once_->insert(header_path).second) continue;

            program.imported_headers.push_back(header_path);
            std::ifstream in(header_path);
            std::ostringstream buf; buf << in.rdbuf();
            Lexer hdr_lexer(buf.str());
            Parser hdr_parser(hdr_lexer.tokenize(), source_dir_, import_paths_, imported_once_);
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
                program.slids.push_back(std::move(slid));
            }

            // load template bodies from impl file: foo.slh -> foo.sl
            if (has_templates) {
                std::string impl_path = header_path.substr(0, header_path.size() - 4) + ".sl";
                std::ifstream impl_in(impl_path);
                if (impl_in) {
                    program.imported_headers.push_back(impl_path);
                    std::ostringstream impl_buf; impl_buf << impl_in.rdbuf();
                    Lexer impl_lexer(impl_buf.str());
                    Parser impl_parser(impl_lexer.tokenize(), source_dir_, import_paths_, imported_once_);
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
                                prog_slid = std::move(impl_slid);
                                replaced = true;
                                break;
                            }
                        }
                        if (!replaced)
                            program.slids.push_back(std::move(impl_slid));
                    }
                }
            }
        }
        // enum definition
        else if (peek().type == TokenType::kEnum) {
            program.enums.push_back(parseEnumDef());
        }
        // explicit template instantiation: Name<Types>;
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
            expect(TokenType::kSemicolon, "expected ';'");
            program.instantiations.push_back({name, std::move(type_args)});
        }
        // slid class definition: bare identifier immediately followed by ( or <
        // (no return type prefix — that's what makes it a class, not a function)
        else if (peek().type == TokenType::kIdentifier
            && pos_ + 1 < (int)tokens_.size()
            && (tokens_[pos_ + 1].type == TokenType::kLParen
                || tokens_[pos_ + 1].type == TokenType::kLt)) {
            program.slids.push_back(parseSlidDef());
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
    return program;
}

std::unique_ptr<SwitchStmt> Parser::parseSwitchStmt() {
    expect(TokenType::kSwitch, "expected 'switch'");
    expect(TokenType::kLParen, "expected '('");
    auto stmt = std::make_unique<SwitchStmt>();
    stmt->expr = parseExpr();
    expect(TokenType::kRParen, "expected ')'");
    expect(TokenType::kLBrace, "expected '{'");

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        SwitchCase sc;
        if (peek().type == TokenType::kCase) {
            advance();
            sc.value = parseExpr();
            expect(TokenType::kColon, "expected ':'");
        } else if (peek().type == TokenType::kDefault) {
            advance();
            expect(TokenType::kColon, "expected ':'");
            sc.value = nullptr; // default
        } else {
            throw std::runtime_error("Line " + std::to_string(peek().line)
                + ": expected 'case' or 'default' in switch");
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
    }
    return stmt;
}
