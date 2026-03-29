#include "parser.h"
#include <stdexcept>

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), pos_(0) {}

Token& Parser::peek() {
    return tokens_[pos_];
}

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
        case TokenType::kInt:
        case TokenType::kInt8:
        case TokenType::kInt16:
        case TokenType::kInt32:
        case TokenType::kInt64:
        case TokenType::kUint:
        case TokenType::kUint8:
        case TokenType::kUint16:
        case TokenType::kUint32:
        case TokenType::kUint64:
        case TokenType::kFloat32:
        case TokenType::kFloat64:
        case TokenType::kBool:
        case TokenType::kVoid:
            return true;
        default:
            return false;
    }
}

std::string Parser::parseTypeName() {
    if (!isTypeName(peek()))
        throw std::runtime_error("Line " + std::to_string(peek().line) + ": expected type name, got '" + peek().value + "'");
    return advance().value;
}

// expression parsing with precedence:
// parseExpr -> parseAddSub -> parseMulDiv -> parseUnary -> parsePrimary

std::unique_ptr<Expr> Parser::parsePrimary() {
    Token& t = peek();

    if (t.type == TokenType::kIntLiteral) {
        advance();
        return std::make_unique<IntLiteralExpr>(std::stoi(t.value));
    }
    if (t.type == TokenType::kStringLiteral) {
        advance();
        return std::make_unique<StringLiteralExpr>(t.value);
    }
    if (t.type == TokenType::kIdentifier) {
        advance();
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
        auto operand = parsePrimary();
        // represent unary minus as 0 - x
        return std::make_unique<BinaryExpr>("-",
            std::make_unique<IntLiteralExpr>(0),
            std::move(operand));
    }
    return parsePrimary();
}

std::unique_ptr<Expr> Parser::parseMulDiv() {
    auto left = parseUnary();
    while (peek().type == TokenType::kStar ||
           peek().type == TokenType::kSlash ||
           peek().type == TokenType::kPercent) {
        std::string op = advance().value;
        auto right = parseUnary();
        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAddSub() {
    auto left = parseMulDiv();
    while (peek().type == TokenType::kPlus ||
           peek().type == TokenType::kMinus) {
        std::string op = advance().value;
        auto right = parseMulDiv();
        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseExpr() {
    return parseAddSub();
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    Token& t = peek();

    // return statement
    if (t.type == TokenType::kReturn) {
        advance();
        auto expr = parseExpr();
        expect(TokenType::kSemicolon, "expected ';' after return value");
        return std::make_unique<ReturnStmt>(std::move(expr));
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

    // identifier — either assignment or function call
    if (t.type == TokenType::kIdentifier) {
        std::string name = t.value;
        advance();

        // function call: name ( args ) ;
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

        // assignment: name = expr ;
        if (peek().type == TokenType::kEquals) {
            advance();
            auto value = parseExpr();
            expect(TokenType::kSemicolon, "expected ';'");
            return std::make_unique<AssignStmt>(name, std::move(value));
        }

        throw std::runtime_error("Line " + std::to_string(peek().line) + ": expected '(' or '=' after identifier '" + name + "'");
    }

    throw std::runtime_error("Line " + std::to_string(t.line) + ": unexpected token '" + t.value + "'");
}

FunctionDef Parser::parseFunctionDef() {
    FunctionDef fn;
    fn.return_type = parseTypeName();
    fn.name = expect(TokenType::kIdentifier, "expected function name").value;
    expect(TokenType::kLParen, "expected '('");
    expect(TokenType::kRParen, "expected ')'");
    expect(TokenType::kLBrace, "expected '{'");
    while (peek().type != TokenType::kRBrace && peek().type != TokenType::kEof)
        fn.body.push_back(parseStmt());
    expect(TokenType::kRBrace, "expected '}'");
    return fn;
}

Program Parser::parse() {
    Program program;
    while (peek().type != TokenType::kEof)
        program.functions.push_back(parseFunctionDef());
    return program;
}
