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

std::unique_ptr<Expr> Parser::parseExpr() {
    Token& t = peek();
    if (t.type == TokenType::kStringLiteral) {
        advance();
        return std::make_unique<StringLiteralExpr>(t.value);
    }
    if (t.type == TokenType::kIntLiteral) {
        advance();
        return std::make_unique<IntLiteralExpr>(std::stoi(t.value));
    }
    throw std::runtime_error("Line " + std::to_string(t.line) + ": expected expression, got '" + t.value + "'");
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

    // function call statement: identifier ( args ) ;
    if (t.type == TokenType::kIdentifier) {
        std::string callee = t.value;
        advance();
        expect(TokenType::kLParen, "expected '(' after function name");
        std::vector<std::unique_ptr<Expr>> args;
        while (peek().type != TokenType::kRParen && peek().type != TokenType::kEof) {
            args.push_back(parseExpr());
            if (peek().type == TokenType::kComma) advance();
        }
        expect(TokenType::kRParen, "expected ')' after arguments");
        expect(TokenType::kSemicolon, "expected ';' after function call");
        return std::make_unique<CallStmt>(callee, std::move(args));
    }

    throw std::runtime_error("Line " + std::to_string(t.line) + ": unexpected token '" + t.value + "'");
}

FunctionDef Parser::parseFunctionDef() {
    FunctionDef fn;

    // return type
    Token& ret = peek();
    if (ret.type == TokenType::kInt32)       { fn.return_type = "int32";  advance(); }
    else if (ret.type == TokenType::kVoid)   { fn.return_type = "void";   advance(); }
    else throw std::runtime_error("Line " + std::to_string(ret.line) + ": expected return type");

    // name
    fn.name = expect(TokenType::kIdentifier, "expected function name").value;

    // params
    expect(TokenType::kLParen, "expected '('");
    // Phase 1: no params supported yet
    expect(TokenType::kRParen, "expected ')'");

    // body
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
