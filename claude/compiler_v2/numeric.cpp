#include "numeric.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "diagnostic.h"
#include "token.h"

namespace numeric {

namespace {

void reportAt(diagnostic::Sink& diag, int file_id, int tok_index,
              std::string const& msg) {
    diagnostic::report(diag, {file_id, tok_index, msg, {}});
}

// kIntLiteral: text is decimal digits (lex stripped underscores). Parse as
// uint64; reject overflow; reject trailing garbage. Text unchanged on success.
bool handleInt(token::Token& t, int tok_index, diagnostic::Sink& diag) {
    try {
        std::size_t pos = 0;
        (void)std::stoull(t.text, &pos, 10);
        if (pos != t.text.size()) {
            reportAt(diag, t.file_id, tok_index, "Integer literal malformed.");
            return false;
        }
        return true;
    } catch (std::out_of_range const&) {
        reportAt(diag, t.file_id, tok_index, "Integer literal overflows uint64.");
        return false;
    } catch (std::invalid_argument const&) {
        reportAt(diag, t.file_id, tok_index, "Integer literal malformed.");
        return false;
    }
}

// kBoolLiteral: lex emits text "true" or "false". Canonicalize to "1" or "0"
// so codegen reads the value directly without a string compare.
bool handleBool(token::Token& t, int tok_index, diagnostic::Sink& diag) {
    if (t.text == "true")       { t.text = "1"; return true; }
    if (t.text == "false")      { t.text = "0"; return true; }
    reportAt(diag, t.file_id, tok_index, "Bool literal malformed.");
    return false;
}

// kCharLiteral: text is the in-quotes content (escapes intact, e.g. "A" or
// "\\n" or "\\'" or "\\q" or "" or "AB"). Interpret escape if present;
// validate exactly one byte; rewrite text to decimal value on success.
bool handleChar(token::Token& t, int tok_index, diagnostic::Sink& diag) {
    std::string const& src = t.text;
    if (src.empty()) {
        reportAt(diag, t.file_id, tok_index, "Empty character literal.");
        return false;
    }
    int value = 0;
    int consumed = 0;
    if (src[0] == '\\') {
        if (src.size() < 2) {
            reportAt(diag, t.file_id, tok_index, "Character literal trailing backslash.");
            return false;
        }
        switch (src[1]) {
            case 'n':  value = '\n'; break;
            case 't':  value = '\t'; break;
            case '0':  value = '\0'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            default:
                reportAt(diag, t.file_id, tok_index,
                    std::string("Unknown escape sequence: '\\") + src[1] + "'.");
                return false;
        }
        consumed = 2;
    } else {
        value = (unsigned char)src[0];
        consumed = 1;
    }
    if ((int)src.size() > consumed) {
        reportAt(diag, t.file_id, tok_index, "Character literal must be a single byte.");
        return false;
    }
    t.text = std::to_string(value);
    return true;
}

// kUintLiteral: text is source-form "0xFFF" or "0b1010" (lex preserves the
// prefix; underscores already stripped). Parse per prefix; reject overflow;
// rewrite text to decimal on success.
bool handleUint(token::Token& t, int tok_index, diagnostic::Sink& diag) {
    bool is_hex = (t.text.size() >= 2 && t.text[0] == '0'
                   && (t.text[1] == 'x' || t.text[1] == 'X'));
    bool is_bin = (t.text.size() >= 2 && t.text[0] == '0'
                   && (t.text[1] == 'b' || t.text[1] == 'B'));
    int base = is_hex ? 16 : (is_bin ? 2 : 0);
    char const* kind_word = is_hex ? "Hex" : "Binary";
    if (base == 0) {
        reportAt(diag, t.file_id, tok_index, "Uint literal malformed.");
        return false;
    }
    std::string digits = t.text.substr(2);
    try {
        std::size_t pos = 0;
        uint64_t uval = std::stoull(digits, &pos, base);
        if (pos != digits.size()) {
            reportAt(diag, t.file_id, tok_index,
                std::string(kind_word) + " literal malformed.");
            return false;
        }
        t.text = std::to_string(uval);
        return true;
    } catch (std::out_of_range const&) {
        reportAt(diag, t.file_id, tok_index,
            std::string(kind_word) + " literal overflows uint64.");
        return false;
    } catch (std::invalid_argument const&) {
        reportAt(diag, t.file_id, tok_index,
            std::string(kind_word) + " literal malformed.");
        return false;
    }
}

// kFloatLiteral: text is source-form minus underscores. Parse as double;
// reject overflow; reject trailing garbage; canonicalize to %.17g on success.
bool handleFloat(token::Token& t, int tok_index, diagnostic::Sink& diag) {
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(t.text.c_str(), &end);
    if (errno == ERANGE) {
        reportAt(diag, t.file_id, tok_index, "Float literal overflows float64.");
        return false;
    }
    if (end == t.text.c_str() || *end != '\0') {
        reportAt(diag, t.file_id, tok_index, "Float literal malformed.");
        return false;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    t.text = buf;
    return true;
}

}  // namespace

void run(token::List& tokens, diagnostic::Sink& diag) {
    for (int i = 0; i < (int)tokens.tokens.size(); ++i) {
        token::Token& t = tokens.tokens[i];
        switch (t.kind) {
            case token::Kind::kIntLiteral:
                if (!handleInt(t, i, diag)) return;
                break;
            case token::Kind::kFloatLiteral:
                if (!handleFloat(t, i, diag)) return;
                break;
            case token::Kind::kUintLiteral:
                if (!handleUint(t, i, diag)) return;
                break;
            case token::Kind::kCharLiteral:
                if (!handleChar(t, i, diag)) return;
                break;
            case token::Kind::kBoolLiteral:
                if (!handleBool(t, i, diag)) return;
                break;
            case token::Kind::kStringLiteral:
            case token::Kind::kIdentifier:
            case token::Kind::kInt:
            case token::Kind::kInt8:
            case token::Kind::kInt16:
            case token::Kind::kInt32:
            case token::Kind::kInt64:
            case token::Kind::kUint:
            case token::Kind::kUint8:
            case token::Kind::kUint16:
            case token::Kind::kUint32:
            case token::Kind::kUint64:
            case token::Kind::kChar:
            case token::Kind::kIntptr:
            case token::Kind::kFloat:
            case token::Kind::kFloat32:
            case token::Kind::kFloat64:
            case token::Kind::kBool:
            case token::Kind::kVoid:
            case token::Kind::kReturn:
            case token::Kind::kIf:
            case token::Kind::kElse:
            case token::Kind::kWhile:
            case token::Kind::kFor:
            case token::Kind::kBreak:
            case token::Kind::kContinue:
            case token::Kind::kEnum:
            case token::Kind::kSwitch:
            case token::Kind::kDefault:
            case token::Kind::kNew:
            case token::Kind::kDelete:
            case token::Kind::kNullptr:
            case token::Kind::kSelf:
            case token::Kind::kImport:
            case token::Kind::kVirtual:
            case token::Kind::kOp:
            case token::Kind::kMutable:
            case token::Kind::kConst:
            case token::Kind::kAlias:
            case token::Kind::kGlobal:
            case token::Kind::kEllipsis:
            case token::Kind::kEqEq:
            case token::Kind::kNotEq:
            case token::Kind::kLt:
            case token::Kind::kGt:
            case token::Kind::kLtEq:
            case token::Kind::kGtEq:
            case token::Kind::kAnd:
            case token::Kind::kOr:
            case token::Kind::kNot:
            case token::Kind::kPlus:
            case token::Kind::kMinus:
            case token::Kind::kStar:
            case token::Kind::kSlash:
            case token::Kind::kPercent:
            case token::Kind::kBitAnd:
            case token::Kind::kBitOr:
            case token::Kind::kBitXor:
            case token::Kind::kBitNot:
            case token::Kind::kLShift:
            case token::Kind::kRShift:
            case token::Kind::kXorXor:
            case token::Kind::kBitAndEq:
            case token::Kind::kBitOrEq:
            case token::Kind::kBitXorEq:
            case token::Kind::kLShiftEq:
            case token::Kind::kRShiftEq:
            case token::Kind::kAndEq:
            case token::Kind::kOrEq:
            case token::Kind::kXorXorEq:
            case token::Kind::kArrowLeft:
            case token::Kind::kArrowBoth:
            case token::Kind::kSizeof:
            case token::Kind::kHash:
            case token::Kind::kHashHash:
            case token::Kind::kEquals:
            case token::Kind::kPlusEq:
            case token::Kind::kMinusEq:
            case token::Kind::kStarEq:
            case token::Kind::kSlashEq:
            case token::Kind::kPercentEq:
            case token::Kind::kPlusPlus:
            case token::Kind::kMinusMinus:
            case token::Kind::kLParen:
            case token::Kind::kRParen:
            case token::Kind::kLBrace:
            case token::Kind::kRBrace:
            case token::Kind::kSemicolon:
            case token::Kind::kComma:
            case token::Kind::kDot:
            case token::Kind::kDotDot:
            case token::Kind::kColon:
            case token::Kind::kColonColon:
            case token::Kind::kLBracket:
            case token::Kind::kRBracket:
            case token::Kind::kEndOfFile:
            case token::Kind::kEndOfInput:
            case token::Kind::kError:
                // n/a: non-numeric-literal tokens flow through untouched.
                break;
        }
    }
}

}  // namespace numeric
