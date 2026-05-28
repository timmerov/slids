#pragma once

#include <deque>
#include <string>
#include <vector>

namespace token {

enum class Kind {
    // literals
    kIntLiteral,     // decimal integer — infers int/int64/uint64
    kUintLiteral,    // hex/binary integer — infers uint/uint64
    kCharLiteral,
    kFloatLiteral,
    kBoolLiteral,    // true / false
    kStringLiteral,

    // identifiers and keywords
    kIdentifier,
    kInt, kInt8, kInt16, kInt32, kInt64,
    kUint, kUint8, kUint16, kUint32, kUint64,
    kChar, kIntptr,
    kFloat, kFloat32, kFloat64,
    kBool, kVoid,
    kReturn,
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

    // terminals
    kEndOfFile,    // per-file: wrapper emits at end of each file's contribution
    kEndOfInput,   // global: lex emits once at the outermost return

    kError,
};

struct Token {
    Kind kind;
    std::string text;
    int file_id;
    int line;
    int col;
    int length;
};

struct File {
    std::string path;
    std::string source;
    std::vector<int> line_starts;
    int imported_by;
};

struct List {
    std::vector<Token> tokens;
    std::deque<File> files;     // deque for stable references — Stream holds a pointer to source
};

void add(List& list, Token const& tok);
int openFile(List& list, std::string path, std::string source, int imported_by = -1);

}  // namespace token
