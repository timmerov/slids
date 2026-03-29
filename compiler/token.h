#pragma once
#include <string>

enum class TokenType {
    // literals
    kIntLiteral,
    kStringLiteral,

    // identifiers and keywords
    kIdentifier,
    kInt,
    kInt8,
    kInt16,
    kInt32,
    kInt64,
    kUint,
    kUint8,
    kUint16,
    kUint32,
    kUint64,
    kFloat32,
    kFloat64,
    kBool,
    kVoid,
    kReturn,
    kTrue,
    kFalse,

    // operators
    kPlus,
    kMinus,
    kStar,
    kSlash,
    kPercent,
    kEquals,

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
