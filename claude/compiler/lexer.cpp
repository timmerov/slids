#include "lexer.h"
#include <cstdint>
#include <stdexcept>

Lexer::Lexer(const std::string& source)
    : source_(source), pos_(0), line_(1) {}

char Lexer::peek() {
    if (pos_ >= (int)source_.size()) return '\0';
    return source_[pos_];
}

char Lexer::peek2() {
    if (pos_ + 1 >= (int)source_.size()) return '\0';
    return source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') line_++;
    return c;
}

void Lexer::skipWhitespaceAndComments() {
    while (pos_ < (int)source_.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance();
        } else if (c == '/' && peek2() == '/') {
            while (pos_ < (int)source_.size() && peek() != '\n')
                advance();
        } else if (c == '/' && peek2() == '*') {
            advance(); advance();
            while (pos_ + 1 < (int)source_.size()) {
                if (peek() == '*' && peek2() == '/') {
                    advance(); advance();
                    break;
                }
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::readCharLiteral() {
    advance(); // consume opening '
    int value = 0;
    if (peek() == '\\') {
        advance();
        char esc = advance();
        switch (esc) {
            case 'n':  value = '\n'; break;
            case 't':  value = '\t'; break;
            case '0':  value = '\0'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            default:   value = esc;  break;
        }
    } else {
        value = (unsigned char)advance();
    }
    if (peek() == '\'') advance(); // consume closing '
    return Token(TokenType::kCharLiteral, std::to_string(value), line_);
}

Token Lexer::readString() {
    advance();
    std::string value;
    while (pos_ < (int)source_.size() && peek() != '"') {
        char c = advance();
        if (c == '\\') {
            char esc = advance();
            switch (esc) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '\n': break;
                default:   value += esc;  break;
            }
        } else {
            value += c;
        }
    }
    if (peek() == '"') advance();
    return Token(TokenType::kStringLiteral, value, line_);
}

Token Lexer::readNumber() {
    std::string value;
    // check for 0x or 0b prefix
    if (peek() == '0' && (peek2() == 'x' || peek2() == 'X')) {
        advance(); advance(); // consume 0x
        std::string digits;
        while (pos_ < (int)source_.size() && (isxdigit(peek()) || peek() == '_'))
            digits += advance();
        // strip underscores and convert hex to decimal
        std::string clean;
        for (char c : digits) if (c != '_') clean += c;
        // use stoull to handle the full unsigned range, then reinterpret as signed
        uint64_t uval = std::stoull(clean, nullptr, 16);
        return Token(TokenType::kUintLiteral, std::to_string(uval), line_);
    }
    if (peek() == '0' && (peek2() == 'b' || peek2() == 'B')) {
        advance(); advance(); // consume 0b
        std::string digits;
        while (pos_ < (int)source_.size() && (peek() == '0' || peek() == '1' || peek() == '_'))
            digits += advance();
        std::string clean;
        for (char c : digits) if (c != '_') clean += c;
        uint64_t uval = std::stoull(clean, nullptr, 2);
        return Token(TokenType::kUintLiteral, std::to_string(uval), line_);
    }
    // decimal
    while (pos_ < (int)source_.size() && (isdigit(peek()) || peek() == '_'))
        value += advance();
    std::string clean;
    for (char c : value) if (c != '_') clean += c;
    // float literal: digits followed by '.' (but not '..' range operator)
    if (peek() == '.' && peek2() != '.') {
        clean += advance(); // consume '.'
        while (pos_ < (int)source_.size() && (isdigit(peek()) || peek() == '_')) {
            char ch = advance();
            if (ch != '_') clean += ch;
        }
        return Token(TokenType::kFloatLiteral, clean, line_);
    }
    return Token(TokenType::kIntLiteral, clean, line_);
}

Token Lexer::readIdentifierOrKeyword() {
    std::string value;
    while (pos_ < (int)source_.size() && (isalnum(peek()) || peek() == '_'))
        value += advance();

    if (value == "int")      return Token(TokenType::kInt,      value, line_);
    if (value == "int8")     return Token(TokenType::kInt8,     value, line_);
    if (value == "int16")    return Token(TokenType::kInt16,    value, line_);
    if (value == "int32")    return Token(TokenType::kInt32,    value, line_);
    if (value == "int64")    return Token(TokenType::kInt64,    value, line_);
    if (value == "uint")     return Token(TokenType::kUint,     value, line_);
    if (value == "uint8")    return Token(TokenType::kUint8,    value, line_);
    if (value == "uint16")   return Token(TokenType::kUint16,   value, line_);
    if (value == "uint32")   return Token(TokenType::kUint32,   value, line_);
    if (value == "uint64")   return Token(TokenType::kUint64,   value, line_);
    if (value == "char")     return Token(TokenType::kChar,     value, line_);
    if (value == "intptr")   return Token(TokenType::kIntptr,   value, line_);
    if (value == "float32")  return Token(TokenType::kFloat32,  value, line_);
    if (value == "float64")  return Token(TokenType::kFloat64,  value, line_);
    if (value == "bool")     return Token(TokenType::kBool,     value, line_);
    if (value == "void")     return Token(TokenType::kVoid,     value, line_);
    if (value == "return")   return Token(TokenType::kReturn,   value, line_);
    if (value == "true")     return Token(TokenType::kTrue,     value, line_);
    if (value == "false")    return Token(TokenType::kFalse,    value, line_);
    if (value == "if")       return Token(TokenType::kIf,       value, line_);
    if (value == "else")     return Token(TokenType::kElse,     value, line_);
    if (value == "while")    return Token(TokenType::kWhile,    value, line_);
    if (value == "for")      return Token(TokenType::kFor,      value, line_);
    if (value == "in")       return Token(TokenType::kIn,       value, line_);
    if (value == "break")    return Token(TokenType::kBreak,    value, line_);
    if (value == "continue") return Token(TokenType::kContinue, value, line_);
    if (value == "enum")     return Token(TokenType::kEnum,     value, line_);
    if (value == "switch")   return Token(TokenType::kSwitch,   value, line_);
    if (value == "case")     return Token(TokenType::kCase,     value, line_);
    if (value == "default")  return Token(TokenType::kDefault,  value, line_);
    if (value == "new")      return Token(TokenType::kNew,      value, line_);
    if (value == "delete")   return Token(TokenType::kDelete,   value, line_);
    if (value == "nullptr")  return Token(TokenType::kNullptr,  value, line_);
    if (value == "import")      return Token(TokenType::kImport,      value, line_);
    if (value == "sizeof")   return Token(TokenType::kSizeof,   value, line_);

    return Token(TokenType::kIdentifier, value, line_);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skipWhitespaceAndComments();
        if (pos_ >= (int)source_.size()) {
            tokens.emplace_back(TokenType::kEof, "", line_);
            break;
        }

        char c = peek();

        if (c == '"')                    { tokens.push_back(readString()); }
        else if (c == '\'')              { tokens.push_back(readCharLiteral()); }
        else if (isdigit(c))             { tokens.push_back(readNumber()); }
        else if (isalpha(c) || c == '_') { tokens.push_back(readIdentifierOrKeyword()); }
        else {
            advance();
            switch (c) {
                case '(': tokens.emplace_back(TokenType::kLParen,    "(", line_); break;
                case ')': tokens.emplace_back(TokenType::kRParen,    ")", line_); break;
                case '{': tokens.emplace_back(TokenType::kLBrace,    "{", line_); break;
                case '}': tokens.emplace_back(TokenType::kRBrace,    "}", line_); break;
                case ';': tokens.emplace_back(TokenType::kSemicolon, ";", line_); break;
                case ',': tokens.emplace_back(TokenType::kComma,     ",", line_); break;
                case '+':
                    if (peek() == '+')      { advance(); tokens.emplace_back(TokenType::kPlusPlus,  "++", line_); }
                    else if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kPlusEq,    "+=", line_); }
                    else                    { tokens.emplace_back(TokenType::kPlus,      "+",  line_); }
                    break;
                case '-':
                    if (peek() == '-')      { advance(); tokens.emplace_back(TokenType::kMinusMinus, "--", line_); }
                    else if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kMinusEq,   "-=", line_); }
                    else                    { tokens.emplace_back(TokenType::kMinus,     "-",  line_); }
                    break;
                case '*':
                    if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kStarEq,    "*=", line_); }
                    else               { tokens.emplace_back(TokenType::kStar,      "*",  line_); }
                    break;
                case '/':
                    if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kSlashEq,   "/=", line_); }
                    else               { tokens.emplace_back(TokenType::kSlash,     "/",  line_); }
                    break;
                case '%':
                    if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kPercentEq, "%=", line_); }
                    else               { tokens.emplace_back(TokenType::kPercent,   "%",  line_); }
                    break;
                case '!':
                    if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kNotEq, "!=", line_); }
                    else               { tokens.emplace_back(TokenType::kNot, "!", line_); }
                    break;
                case '=':
                    if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kEqEq,  "==", line_); }
                    else               { tokens.emplace_back(TokenType::kEquals, "=", line_); }
                    break;
                case '<':
                    if (peek() == '<') {
                        advance();
                        if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kLShiftEq, "<<=", line_); }
                        else               { tokens.emplace_back(TokenType::kLShift, "<<", line_); }
                    } else if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kLtEq,     "<=", line_); }
                    else if (peek() == '-') {
                        advance();
                        if (peek() == '>') { advance(); tokens.emplace_back(TokenType::kArrowBoth, "<->", line_); }
                        else               { tokens.emplace_back(TokenType::kArrowLeft, "<-", line_); }
                    }
                    else                   { tokens.emplace_back(TokenType::kLt,        "<",  line_); }
                    break;
                case '>':
                    if (peek() == '>') {
                        advance();
                        if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kRShiftEq, ">>=", line_); }
                        else               { tokens.emplace_back(TokenType::kRShift, ">>", line_); }
                    } else if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kGtEq, ">=", line_); }
                    else                      { tokens.emplace_back(TokenType::kGt,   ">",  line_); }
                    break;
                case '&':
                    if (peek() == '&') {
                        advance();
                        if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kAndEq,    "&&=", line_); }
                        else               { tokens.emplace_back(TokenType::kAnd,       "&&",  line_); }
                    } else if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kBitAndEq, "&=", line_); }
                    else                      { tokens.emplace_back(TokenType::kBitAnd,  "&",   line_); }
                    break;
                case '|':
                    if (peek() == '|') {
                        advance();
                        if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kOrEq,     "||=", line_); }
                        else               { tokens.emplace_back(TokenType::kOr,        "||",  line_); }
                    } else if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kBitOrEq,  "|=", line_); }
                    else                      { tokens.emplace_back(TokenType::kBitOr,   "|",   line_); }
                    break;
                case '^':
                    if (peek() == '^') {
                        advance();
                        if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kXorXorEq, "^^=", line_); }
                        else               { tokens.emplace_back(TokenType::kXorXor,    "^^",  line_); }
                    } else if (peek() == '=') { advance(); tokens.emplace_back(TokenType::kBitXorEq, "^=", line_); }
                    else                      { tokens.emplace_back(TokenType::kBitXor,  "^",   line_); }
                    break;
                case '~':
                    tokens.emplace_back(TokenType::kBitNot, "~", line_);
                    break;
                case '.':
                    if (peek() == '.' && pos_ + 1 < (int)source_.size() && source_[pos_ + 1] == '.') {
                        advance(); advance();
                        tokens.emplace_back(TokenType::kEllipsis, "...", line_);
                    } else if (peek() == '.') {
                        advance();
                        tokens.emplace_back(TokenType::kDotDot, "..", line_);
                    } else {
                        tokens.emplace_back(TokenType::kDot, ".", line_);
                    }
                    break;
                case ':':
                    tokens.emplace_back(TokenType::kColon, ":", line_);
                    break;
                case '[':
                    if (peek() == ']' && peek2() == '=') {
                        advance(); advance();
                        tokens.emplace_back(TokenType::kBracketAssign, "[]=", line_);
                    } else {
                        tokens.emplace_back(TokenType::kLBracket, "[", line_);
                    }
                    break;
                case ']':
                    tokens.emplace_back(TokenType::kRBracket, "]", line_);
                    break;
                default:  tokens.emplace_back(TokenType::kUnknown, std::string(1, c), line_); break;
            }
        }
    }

    return tokens;
}
