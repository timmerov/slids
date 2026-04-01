#include "parser.h"
#include <stdexcept>
#include <map>

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), pos_(0) {}

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

bool Parser::isTypeName(const Token& t) {
    switch (t.type) {
        case TokenType::kInt: case TokenType::kInt8: case TokenType::kInt16:
        case TokenType::kInt32: case TokenType::kInt64:
        case TokenType::kUint: case TokenType::kUint8: case TokenType::kUint16:
        case TokenType::kUint32: case TokenType::kUint64:
        case TokenType::kFloat32: case TokenType::kFloat64:
        case TokenType::kBool: case TokenType::kVoid:
            return true;
        default: return false;
    }
}

bool Parser::isUserTypeName(const Token& t) {
    // user-defined type: identifier starting with uppercase
    return t.type == TokenType::kIdentifier
        && !t.value.empty()
        && isupper(t.value[0]);
}

std::string Parser::parseTypeName() {
    if (isTypeName(peek())) return advance().value;
    if (isUserTypeName(peek())) return advance().value;
    throw std::runtime_error("Line " + std::to_string(peek().line)
        + ": expected type name, got '" + peek().value + "'");
}

// --- Expression parsing ---

std::unique_ptr<Expr> Parser::parsePrimary() {
    Token t = peek();
    if (t.type == TokenType::kIntLiteral) {
        advance();
        return std::make_unique<IntLiteralExpr>(std::stoi(t.value));
    }
    if (t.type == TokenType::kStringLiteral) {
        advance();
        return std::make_unique<StringLiteralExpr>(t.value);
    }
    if (t.type == TokenType::kTrue)  { advance(); return std::make_unique<IntLiteralExpr>(1); }
    if (t.type == TokenType::kFalse) { advance(); return std::make_unique<IntLiteralExpr>(0); }
    if (t.type == TokenType::kIdentifier) {
        advance();
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
    if (t.type == TokenType::kLParen) {
        advance();
        auto expr = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        return expr;
    }
    throw std::runtime_error("Line " + std::to_string(t.line)
        + ": expected expression, got '" + t.value + "'");
}

// postfix: handle .field and .method(args) chaining
std::unique_ptr<Expr> Parser::parsePostfix(std::unique_ptr<Expr> base) {
    while (true) {
        if (peek().type == TokenType::kDot) {
            advance();
            std::string member = expect(TokenType::kIdentifier, "expected field or method name").value;
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
        } else if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
            std::string op = (peek().type == TokenType::kPlusPlus) ? "post++" : "post--";
            advance();
            base = std::make_unique<UnaryExpr>(op, std::move(base));
        } else {
            break;
        }
    }
    return base;
}

std::unique_ptr<Expr> Parser::parseUnary() {
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
    // pre-increment/decrement: ++x, --x  (desugar to x += 1, returns new value)
    if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
        std::string op = (peek().type == TokenType::kPlusPlus) ? "+" : "-";
        advance();
        auto operand = parsePrimary();
        // return x + 1 (or x - 1) — the store happens via UnaryExpr in codegen
        return std::make_unique<UnaryExpr>(op == "+" ? "pre++" : "pre--", std::move(operand));
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

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    expect(TokenType::kLBrace, "expected '{'");
    auto block = std::make_unique<BlockStmt>();
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof)
        block->stmts.push_back(parseStmt());
    expect(TokenType::kRBrace, "expected '}'");
    return block;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    Token t = peek();

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
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<BreakStmt>();
    }

    if (t.type == TokenType::kContinue) {
        advance();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<ContinueStmt>();
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
        expect(TokenType::kLParen, "expected '('");
        stmt->cond = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        stmt->body = parseBlock();
        return stmt;
    }

    if (t.type == TokenType::kFor) {
        advance();
        auto stmt = std::make_unique<ForRangeStmt>();
        stmt->var_type = parseTypeName();
        stmt->var_name = expect(TokenType::kIdentifier, "expected variable name").value;
        expect(TokenType::kIn, "expected 'in'");
        expect(TokenType::kLParen, "expected '('");
        stmt->range_start = parseExpr();
        expect(TokenType::kDotDot, "expected '..'");
        stmt->range_end = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        stmt->body = parseBlock();
        return stmt;
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
        expect(TokenType::kEquals, "expected '='");
        auto init = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<VarDeclStmt>(type, name, std::move(init));
    }

    // user-defined type variable declaration: Counter c; or Counter c(5);
    if (isUserTypeName(t)) {
        std::string type = advance().value;
        std::string name = expect(TokenType::kIdentifier, "expected variable name").value;
        std::vector<std::unique_ptr<Expr>> ctor_args;
        if (peek().type == TokenType::kLParen) {
            advance();
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                ctor_args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
        }
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<VarDeclStmt>(type, name, nullptr, std::move(ctor_args));
    }

    // identifier — assignment, compound assignment, method call, or function call
    if (t.type == TokenType::kIdentifier) {
        std::string name = t.value;
        advance();

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

        // post-increment/decrement statement: x++; x--;
        if (peek().type == TokenType::kPlusPlus || peek().type == TokenType::kMinusMinus) {
            std::string op = (peek().type == TokenType::kPlusPlus) ? "+" : "-";
            advance();
            expect(TokenType::kSemicolon, "expected ';'");
            // desugar to x = x + 1
            auto lhs = std::make_unique<VarExpr>(name);
            auto rhs = std::make_unique<BinaryExpr>(op, std::make_unique<VarExpr>(name),
                                                        std::make_unique<IntLiteralExpr>(1));
            return std::make_unique<AssignStmt>(name, std::move(rhs));
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

        // plain assignment
        if (peek().type == TokenType::kEquals) {
            advance();
            auto value = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            return std::make_unique<AssignStmt>(name, std::move(value));
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

        throw std::runtime_error("Line " + std::to_string(peek().line)
            + ": expected '(' or '=' after '" + name + "'");
    }

    throw std::runtime_error("Line " + std::to_string(t.line)
        + ": unexpected token '" + t.value + "'");
}

// --- Top-level parsing ---

NestedFunctionDef Parser::parseNestedFunctionDef() {
    NestedFunctionDef fn;
    fn.return_type = parseTypeName();
    fn.name = expect(TokenType::kIdentifier, "expected function name").value;
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        fn.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    fn.body = parseBlock();
    return fn;
}

MethodDef Parser::parseMethodDef() {
    MethodDef m;
    m.return_type = parseTypeName();
    m.name = expect(TokenType::kIdentifier, "expected method name").value;
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        m.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    m.body = parseBlock();
    return m;
}

SlidDef Parser::parseSlidDef() {
    SlidDef slid;
    slid.name = peek().value;
    advance(); // consume class name

    // parse tuple: (type field_ = default, ...)
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        FieldDef f;
        f.type = parseTypeName();
        f.name = expect(TokenType::kIdentifier, "expected field name").value;
        if (peek().type == TokenType::kEquals) {
            advance();
            f.default_val = parseExpr();
        }
        slid.fields.push_back(std::move(f));
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");

    // parse body: methods and optional constructor code
    expect(TokenType::kLBrace, "expected '{'");

    auto ctor_body = std::make_unique<BlockStmt>();
    bool has_ctor_code = false;

    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof) {
        // method definition: starts with a type name followed by identifier followed by (
        if ((isTypeName(peek()) || isUserTypeName(peek()))
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kIdentifier
            && pos_ + 2 < (int)tokens_.size()
            && tokens_[pos_ + 2].type == TokenType::kLParen) {
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
    return slid;
}

FunctionDef Parser::parseFunctionDef() {
    FunctionDef fn;
    fn.return_type = parseTypeName();
    fn.name = expect(TokenType::kIdentifier, "expected function name").value;
    expect(TokenType::kLParen, "expected '('");
    while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected parameter name").value;
        fn.params.emplace_back(type, name);
        if (peek().type == TokenType::kComma) advance();
    }
    expect(TokenType::kRParen, "expected ')'");
    fn.body = parseBlock();
    return fn;
}

Program Parser::parse() {
    Program program;
    while (peek().type != TokenType::kEof) {
        // slid class definition: UpperCase identifier followed by (
        if (isUserTypeName(peek())
            && pos_ + 1 < (int)tokens_.size()
            && tokens_[pos_ + 1].type == TokenType::kLParen) {
            program.slids.push_back(parseSlidDef());
        } else {
            program.functions.push_back(parseFunctionDef());
        }
    }
    return program;
}
