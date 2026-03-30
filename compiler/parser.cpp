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
        throw std::runtime_error("Line " + std::to_string(peek().line) + ": " + msg + ", got '" + peek().value + "'");
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

std::string Parser::parseTypeName() {
    if (!isTypeName(peek()))
        throw std::runtime_error("Line " + std::to_string(peek().line) + ": expected type name, got '" + peek().value + "'");
    return advance().value;
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
    if (t.type == TokenType::kTrue) {
        advance();
        return std::make_unique<IntLiteralExpr>(1);
    }
    if (t.type == TokenType::kFalse) {
        advance();
        return std::make_unique<IntLiteralExpr>(0);
    }
    if (t.type == TokenType::kIdentifier) {
        advance();
        // function call expression: name(args)
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
    throw std::runtime_error("Line " + std::to_string(t.line) + ": expected expression, got '" + t.value + "'");
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (peek().type == TokenType::kMinus) {
        advance();
        return std::make_unique<BinaryExpr>("-",
            std::make_unique<IntLiteralExpr>(0),
            parsePrimary());
    }
    if (peek().type == TokenType::kNot) {
        advance();
        return std::make_unique<UnaryExpr>("!", parseUnary());
    }
    if (peek().type == TokenType::kBitNot) {
        advance();
        return std::make_unique<UnaryExpr>("~", parseUnary());
    }
    return parsePrimary();
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

    // return
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

    // break
    if (t.type == TokenType::kBreak) {
        advance();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<BreakStmt>();
    }

    // continue
    if (t.type == TokenType::kContinue) {
        advance();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<ContinueStmt>();
    }

    // if
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
                // else if — wrap in a block
                auto else_block = std::make_unique<BlockStmt>();
                else_block->stmts.push_back(parseStmt());
                stmt->else_block = std::move(else_block);
            } else {
                stmt->else_block = parseBlock();
            }
        }
        return stmt;
    }

    // while
    if (t.type == TokenType::kWhile) {
        advance();
        auto stmt = std::make_unique<WhileStmt>();
        expect(TokenType::kLParen, "expected '('");
        stmt->cond = parseExpr();
        expect(TokenType::kRParen, "expected ')'");
        stmt->body = parseBlock();
        return stmt;
    }

    // for int i in (start..end)
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

    // variable declaration: type name = expr;
    if (isTypeName(t)) {
        std::string type = parseTypeName();
        std::string name = expect(TokenType::kIdentifier, "expected variable name").value;
        expect(TokenType::kEquals, "expected '='");
        auto init = parseExpr();
        expect(TokenType::kSemicolon, "expected ';'");
        return std::make_unique<VarDeclStmt>(type, name, std::move(init));
    }

    // identifier — assignment or function call statement
    if (t.type == TokenType::kIdentifier) {
        std::string name = t.value;
        advance();

        if (peek().type == TokenType::kLParen) {
            // parse as expression then wrap in CallStmt
            std::vector<std::unique_ptr<Expr>> args;
            advance();
            while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
                args.push_back(parseExpr());
                if (peek().type == TokenType::kComma) advance();
            }
            expect(TokenType::kRParen, "expected ')'");
            expect(TokenType::kSemicolon, "expected ';'");
            return std::make_unique<CallStmt>(name, std::move(args));
        }

        if (peek().type == TokenType::kEquals) {
            advance();
            auto value = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            return std::make_unique<AssignStmt>(name, std::move(value));
        }

        // compound assignment: desugar x += expr into x = x + expr
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
    while (peek().type != TokenType::kEof)
        program.functions.push_back(parseFunctionDef());
    return program;
}
