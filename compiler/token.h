#pragma once
#include <string>

enum class TokenType {
    // literals
    kIntLiteral,
    kStringLiteral,

    // identifiers and keywords
    kIdentifier,
    kInt32,
    kVoid,
    kReturn,

    // punctuation
    kLParen,
    kRParen,
    kLBrace,
    kRBrace,
    kSemicolon,
    kComma,

    kEof,
    kUnknown,
};

struct Token {
    TokenType type;
    std::string value;
    int line;

    Token(TokenType type, std::string value, int line)
        : type(type), value(std::move(value)), line(line) {}
};
