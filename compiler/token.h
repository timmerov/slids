#pragma once
#include <string>

enum class TokenType {
    // literals
    kIntLiteral,
    kCharLiteral,
    kFloatLiteral,
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
    kChar,
    kIntptr,
    kFloat32,
    kFloat64,
    kBool,
    kVoid,
    kReturn,
    kTrue,
    kFalse,
    kIf,
    kElse,
    kWhile,
    kFor,
    kIn,
    kBreak,
    kContinue,
    kEnum,
    kSwitch,
    kCase,
    kDefault,
    kNew,
    kDelete,
    kNullptr,
    kImport,

    // comparison operators
    kEqEq,
    kNotEq,
    kLt,
    kGt,
    kLtEq,
    kGtEq,

    // logical operators
    kAnd,
    kOr,
    kNot,

    // arithmetic operators
    kPlus,
    kMinus,
    kStar,
    kSlash,
    kPercent,

    // bitwise operators
    kBitAnd,
    kBitOr,
    kBitXor,
    kBitNot,
    kLShift,
    kRShift,

    // logical xor
    kXorXor,

    // augmented bitwise assignments
    kBitAndEq,
    kBitOrEq,
    kBitXorEq,
    kLShiftEq,
    kRShiftEq,

    // augmented logical assignments
    kAndEq,
    kOrEq,
    kXorXorEq,

    // move and swap operators
    kArrowLeft,   // <-
    kArrowBoth,   // <->

    // assignment
    kEquals,
    kPlusEq,
    kMinusEq,
    kStarEq,
    kSlashEq,
    kPercentEq,
    kPlusPlus,
    kMinusMinus,

    // punctuation
    kLParen,
    kRParen,
    kLBrace,
    kRBrace,
    kSemicolon,
    kComma,
    kDot,
    kDotDot,
    kColon,
    kLBracket,
    kRBracket,

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
