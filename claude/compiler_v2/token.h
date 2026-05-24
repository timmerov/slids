#pragma once

#include <string>
#include <vector>

namespace token {

enum class Kind {
    // literals
    kIntLiteral,     // decimal integer — infers int/int64/uint64
    kUintLiteral,    // hex/binary integer — infers uint/uint64
    kCharLiteral,
    kFloatLiteral,
    kStringLiteral,

    // identifiers and keywords
    kIdentifier,
    kInt, kInt8, kInt16, kInt32, kInt64,
    kUint, kUint8, kUint16, kUint32, kUint64,
    kChar, kIntptr,
    kFloat, kFloat32, kFloat64,
    kBool, kVoid,
    kReturn, kTrue, kFalse,
    kIf, kElse, kWhile, kFor, kBreak, kContinue,
    kEnum, kSwitch, kCase, kDefault,
    kNew, kDelete, kNullptr, kSelf, kImport, kVirtual,
    kOp, kMutable, kConst, kAlias, kGlobal,
    kEllipsis,

    // comparison
    kEqEq, kNotEq, kLt, kGt, kLtEq, kGtEq,

    // logical
    kAnd, kOr, kNot,

    // arithmetic
    kPlus, kMinus, kStar, kSlash, kPercent,

    // bitwise
    kBitAnd, kBitOr, kBitXor, kBitNot, kLShift, kRShift,

    // logical xor
    kXorXor,

    // augmented bitwise assigns
    kBitAndEq, kBitOrEq, kBitXorEq, kLShiftEq, kRShiftEq,

    // augmented logical assigns
    kAndEq, kOrEq, kXorXorEq,

    // move/swap
    kArrowLeft,   // <--
    kArrowBoth,   // <-->

    kSizeof,

    // stringify
    kHash,        // #
    kHashHash,    // ##

    // assignment
    kEquals,
    kPlusEq, kMinusEq, kStarEq, kSlashEq, kPercentEq,
    kPlusPlus, kMinusMinus,

    // punctuation
    kLParen, kRParen, kLBrace, kRBrace,
    kSemicolon, kComma,
    kDot, kDotDot,
    kColon, kColonColon,
    kLBracket, kRBracket,

    kEof,
    kUnknown,
    kError,
};

struct Token {
    Kind kind;
    std::string text;
    int line;
    int col;
    int length;
};

struct List {
    std::vector<Token> tokens;
};

void add(List& list, Token const& tok);

}  // namespace token
