#include "lexer.h"
#include "source_map.h"
#include <cctype>
#include <cstdint>

Lexer::Lexer(SourceMap& sm, int file_id)
    : sm_(sm), file_id_(file_id),
      source_(sm.at(file_id).source),
      pos_(0), line_(1), col_(1) {}

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
    if (c == '\n') { line_++; col_ = 1; }
    else { col_++; }
    return c;
}

void Lexer::throwLexError(int line, int col, int length, const std::string& msg) {
    auto& tlocs = sm_.at(file_id_).tokens;
    tlocs.push_back({line, col, length});
    throw CompileError{file_id_, (int)tlocs.size() - 1, msg};
}

void Lexer::skipWhitespaceAndComments() {
    int depth = 0;
    bool in_line = false;
    while (pos_ < (int)source_.size()) {
        char c = peek();
        char d = peek2();
        if (c == '/' && d == '/') {
            in_line = true;
            advance(); advance();
            continue;
        }
        if (c == '/' && d == '*') {
            depth++;
            advance(); advance();
            continue;
        }
        if (c == '*' && d == '/') {
            if (depth == 0)
                throwLexError(line_, col_, 2, "unmatched */");
            depth--;
            advance(); advance();
            continue;
        }
        if (in_line) {
            if (c == '\n') {
                in_line = false;
                advance();
                continue;
            }
            if (c == '\\') {
                int p = pos_ + 1;
                while (p < (int)source_.size()
                    && (source_[p] == ' ' || source_[p] == '\t'
                        || source_[p] == '\r')) p++;
                if (p < (int)source_.size() && source_[p] == '\n') {
                    if (p != pos_ + 1)
                        throwLexError(line_, col_, 1,
                            "whitespace between line-continuation \\ and newline");
                    advance(); advance();
                    continue;
                }
            }
            advance();
            continue;
        }
        if (depth > 0) {
            advance();
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance();
            continue;
        }
        break;
    }
    if (depth > 0)
        throwLexError(line_, col_, 1, "unterminated block comment");
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
    return Token(TokenType::kCharLiteral, std::to_string(value));
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
    return Token(TokenType::kStringLiteral, value);
}

Token Lexer::readNumber() {
    std::string value;
    if (peek() == '0' && (peek2() == 'x' || peek2() == 'X')) {
        advance(); advance();
        std::string digits;
        while (pos_ < (int)source_.size() && (isxdigit(peek()) || peek() == '_'))
            digits += advance();
        std::string clean;
        for (char c : digits) if (c != '_') clean += c;
        uint64_t uval = std::stoull(clean, nullptr, 16);
        return Token(TokenType::kUintLiteral, std::to_string(uval));
    }
    if (peek() == '0' && (peek2() == 'b' || peek2() == 'B')) {
        advance(); advance();
        std::string digits;
        while (pos_ < (int)source_.size() && (peek() == '0' || peek() == '1' || peek() == '_'))
            digits += advance();
        std::string clean;
        for (char c : digits) if (c != '_') clean += c;
        uint64_t uval = std::stoull(clean, nullptr, 2);
        return Token(TokenType::kUintLiteral, std::to_string(uval));
    }
    while (pos_ < (int)source_.size() && (isdigit(peek()) || peek() == '_'))
        value += advance();
    std::string clean;
    for (char c : value) if (c != '_') clean += c;
    if (peek() == '.' && peek2() != '.') {
        clean += advance();
        while (pos_ < (int)source_.size() && (isdigit(peek()) || peek() == '_')) {
            char ch = advance();
            if (ch != '_') clean += ch;
        }
        return Token(TokenType::kFloatLiteral, clean);
    }
    return Token(TokenType::kIntLiteral, clean);
}

Token Lexer::readIdentifierOrKeyword() {
    std::string value;
    while (pos_ < (int)source_.size() && (isalnum(peek()) || peek() == '_'))
        value += advance();

    if (value == "int")      return Token(TokenType::kInt,      value);
    if (value == "int8")     return Token(TokenType::kInt8,     value);
    if (value == "int16")    return Token(TokenType::kInt16,    value);
    if (value == "int32")    return Token(TokenType::kInt32,    value);
    if (value == "int64")    return Token(TokenType::kInt64,    value);
    if (value == "uint")     return Token(TokenType::kUint,     value);
    if (value == "uint8")    return Token(TokenType::kUint8,    value);
    if (value == "uint16")   return Token(TokenType::kUint16,   value);
    if (value == "uint32")   return Token(TokenType::kUint32,   value);
    if (value == "uint64")   return Token(TokenType::kUint64,   value);
    if (value == "char")     return Token(TokenType::kChar,     value);
    if (value == "intptr")   return Token(TokenType::kIntptr,   value);
    if (value == "float32")  return Token(TokenType::kFloat32,  value);
    if (value == "float64")  return Token(TokenType::kFloat64,  value);
    if (value == "bool")     return Token(TokenType::kBool,     value);
    if (value == "void")     return Token(TokenType::kVoid,     value);
    if (value == "return")   return Token(TokenType::kReturn,   value);
    if (value == "true")     return Token(TokenType::kTrue,     value);
    if (value == "false")    return Token(TokenType::kFalse,    value);
    if (value == "if")       return Token(TokenType::kIf,       value);
    if (value == "else")     return Token(TokenType::kElse,     value);
    if (value == "while")    return Token(TokenType::kWhile,    value);
    if (value == "for")      return Token(TokenType::kFor,      value);
    if (value == "break")    return Token(TokenType::kBreak,    value);
    if (value == "continue") return Token(TokenType::kContinue, value);
    if (value == "enum")     return Token(TokenType::kEnum,     value);
    if (value == "switch")   return Token(TokenType::kSwitch,   value);
    if (value == "case")     return Token(TokenType::kCase,     value);
    if (value == "default")  return Token(TokenType::kDefault,  value);
    if (value == "new")      return Token(TokenType::kNew,      value);
    if (value == "delete")   return Token(TokenType::kDelete,   value);
    if (value == "nullptr")  return Token(TokenType::kNullptr,  value);
    if (value == "import")   return Token(TokenType::kImport,   value);
    if (value == "virtual")  return Token(TokenType::kVirtual,  value);
    if (value == "sizeof")   return Token(TokenType::kSizeof,   value);
    if (value == "op")       return Token(TokenType::kOp,       value);
    if (value == "mutable")  return Token(TokenType::kMutable,  value);

    return Token(TokenType::kIdentifier, value);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    auto& tlocs = sm_.at(file_id_).tokens;
    int sl = 1, sc = 1, sp = 0;

    auto emit = [&](TokenType t, std::string v) {
        tokens.push_back(Token(t, std::move(v)));
        tlocs.push_back({sl, sc, pos_ - sp});
    };

    while (true) {
        skipWhitespaceAndComments();
        sl = line_; sc = col_; sp = pos_;
        if (pos_ >= (int)source_.size()) {
            tokens.push_back(Token(TokenType::kEof, ""));
            tlocs.push_back({sl, sc, 0});
            break;
        }

        char c = peek();

        if (c == '"')                    { Token t = readString();             emit(t.type, std::move(t.value)); }
        else if (c == '\'')              { Token t = readCharLiteral();        emit(t.type, std::move(t.value)); }
        else if (isdigit(c))             { Token t = readNumber();             emit(t.type, std::move(t.value)); }
        else if (isalpha(c) || c == '_') { Token t = readIdentifierOrKeyword(); emit(t.type, std::move(t.value)); }
        else {
            advance();
            switch (c) {
                case '(': emit(TokenType::kLParen,    "("); break;
                case ')': emit(TokenType::kRParen,    ")"); break;
                case '{': emit(TokenType::kLBrace,    "{"); break;
                case '}': emit(TokenType::kRBrace,    "}"); break;
                case ';': emit(TokenType::kSemicolon, ";"); break;
                case ',': emit(TokenType::kComma,     ","); break;
                case '+':
                    if (peek() == '+')      { advance(); emit(TokenType::kPlusPlus,   "++"); }
                    else if (peek() == '=') { advance(); emit(TokenType::kPlusEq,     "+="); }
                    else                    {            emit(TokenType::kPlus,       "+");  }
                    break;
                case '-':
                    if (peek() == '-')      { advance(); emit(TokenType::kMinusMinus, "--"); }
                    else if (peek() == '=') { advance(); emit(TokenType::kMinusEq,    "-="); }
                    else                    {            emit(TokenType::kMinus,      "-");  }
                    break;
                case '*':
                    if (peek() == '=') { advance(); emit(TokenType::kStarEq,   "*="); }
                    else               {            emit(TokenType::kStar,     "*");  }
                    break;
                case '/':
                    if (peek() == '=') { advance(); emit(TokenType::kSlashEq,  "/="); }
                    else               {            emit(TokenType::kSlash,    "/");  }
                    break;
                case '%':
                    if (peek() == '=') { advance(); emit(TokenType::kPercentEq, "%="); }
                    else               {            emit(TokenType::kPercent,   "%");  }
                    break;
                case '!':
                    if (peek() == '=') { advance(); emit(TokenType::kNotEq, "!="); }
                    else               {            emit(TokenType::kNot,   "!");  }
                    break;
                case '=':
                    if (peek() == '=') { advance(); emit(TokenType::kEqEq,   "=="); }
                    else               {            emit(TokenType::kEquals, "=");  }
                    break;
                case '<':
                    if (peek() == '<') {
                        advance();
                        if (peek() == '=') { advance(); emit(TokenType::kLShiftEq, "<<="); }
                        else               {            emit(TokenType::kLShift,   "<<");  }
                    } else if (peek() == '=') { advance(); emit(TokenType::kLtEq, "<="); }
                    else if (peek() == '-') {
                        advance();
                        if (peek() == '>') { advance(); emit(TokenType::kArrowBoth, "<->"); }
                        else               {            emit(TokenType::kArrowLeft, "<-");  }
                    }
                    else                   {            emit(TokenType::kLt, "<"); }
                    break;
                case '>':
                    if (peek() == '>') {
                        advance();
                        if (peek() == '=') { advance(); emit(TokenType::kRShiftEq, ">>="); }
                        else               {            emit(TokenType::kRShift,   ">>");  }
                    } else if (peek() == '=') { advance(); emit(TokenType::kGtEq, ">="); }
                    else                      {            emit(TokenType::kGt,   ">");  }
                    break;
                case '&':
                    if (peek() == '&') {
                        advance();
                        if (peek() == '=') { advance(); emit(TokenType::kAndEq,    "&&="); }
                        else               {            emit(TokenType::kAnd,      "&&");  }
                    } else if (peek() == '=') { advance(); emit(TokenType::kBitAndEq, "&="); }
                    else                      {            emit(TokenType::kBitAnd,   "&");  }
                    break;
                case '|':
                    if (peek() == '|') {
                        advance();
                        if (peek() == '=') { advance(); emit(TokenType::kOrEq,    "||="); }
                        else               {            emit(TokenType::kOr,      "||");  }
                    } else if (peek() == '=') { advance(); emit(TokenType::kBitOrEq, "|="); }
                    else                      {            emit(TokenType::kBitOr,   "|");  }
                    break;
                case '^':
                    if (peek() == '^') {
                        advance();
                        if (peek() == '=') { advance(); emit(TokenType::kXorXorEq, "^^="); }
                        else               {            emit(TokenType::kXorXor,   "^^");  }
                    } else if (peek() == '=') { advance(); emit(TokenType::kBitXorEq, "^="); }
                    else                      {            emit(TokenType::kBitXor,   "^");  }
                    break;
                case '~':
                    emit(TokenType::kBitNot, "~");
                    break;
                case '.':
                    if (peek() == '.' && pos_ + 1 < (int)source_.size() && source_[pos_ + 1] == '.') {
                        advance(); advance();
                        emit(TokenType::kEllipsis, "...");
                    } else if (peek() == '.') {
                        advance();
                        emit(TokenType::kDotDot, "..");
                    } else {
                        emit(TokenType::kDot, ".");
                    }
                    break;
                case ':':
                    if (peek() == ':') {
                        advance();
                        emit(TokenType::kColonColon, "::");
                    } else {
                        emit(TokenType::kColon, ":");
                    }
                    break;
                case '[':
                    if (peek() == ']' && peek2() == '=') {
                        advance(); advance();
                        emit(TokenType::kBracketAssign, "[]=");
                    } else {
                        emit(TokenType::kLBracket, "[");
                    }
                    break;
                case ']':
                    emit(TokenType::kRBracket, "]");
                    break;
                case '#':
                    if (peek() == '#') {
                        advance();
                        emit(TokenType::kHashHash, "##");
                    } else {
                        emit(TokenType::kHash, "#");
                    }
                    break;
                default:  emit(TokenType::kUnknown, std::string(1, c)); break;
            }
        }
    }

    return tokens;
}
