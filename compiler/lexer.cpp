#include "lexer.h"
#include <stdexcept>

Lexer::Lexer(const std::string& source)
    : source_(source), pos_(0), line_(1) {}

char Lexer::peek() {
    if (pos_ >= (int)source_.size()) return '\0';
    return source_[pos_];
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
        } else if (c == '/' && pos_ + 1 < (int)source_.size()) {
            if (source_[pos_ + 1] == '/') {
                while (pos_ < (int)source_.size() && peek() != '\n')
                    advance();
            } else if (source_[pos_ + 1] == '*') {
                advance(); advance();
                while (pos_ + 1 < (int)source_.size()) {
                    if (peek() == '*' && source_[pos_ + 1] == '/') {
                        advance(); advance();
                        break;
                    }
                    advance();
                }
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

Token Lexer::readString() {
    advance(); // consume opening "
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
    while (pos_ < (int)source_.size() && (isdigit(peek()) || peek() == '_'))
        value += advance();
    std::string clean;
    for (char c : value)
        if (c != '_') clean += c;
    return Token(TokenType::kIntLiteral, clean, line_);
}

Token Lexer::readIdentifierOrKeyword() {
    std::string value;
    while (pos_ < (int)source_.size() && (isalnum(peek()) || peek() == '_'))
        value += advance();

    if (value == "int")     return Token(TokenType::kInt,     value, line_);
    if (value == "int8")    return Token(TokenType::kInt8,    value, line_);
    if (value == "int16")   return Token(TokenType::kInt16,   value, line_);
    if (value == "int32")   return Token(TokenType::kInt32,   value, line_);
    if (value == "int64")   return Token(TokenType::kInt64,   value, line_);
    if (value == "uint")    return Token(TokenType::kUint,    value, line_);
    if (value == "uint8")   return Token(TokenType::kUint8,   value, line_);
    if (value == "uint16")  return Token(TokenType::kUint16,  value, line_);
    if (value == "uint32")  return Token(TokenType::kUint32,  value, line_);
    if (value == "uint64")  return Token(TokenType::kUint64,  value, line_);
    if (value == "float32") return Token(TokenType::kFloat32, value, line_);
    if (value == "float64") return Token(TokenType::kFloat64, value, line_);
    if (value == "bool")    return Token(TokenType::kBool,    value, line_);
    if (value == "void")    return Token(TokenType::kVoid,    value, line_);
    if (value == "return")  return Token(TokenType::kReturn,  value, line_);
    if (value == "true")    return Token(TokenType::kTrue,    value, line_);
    if (value == "false")   return Token(TokenType::kFalse,   value, line_);

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
                case '+': tokens.emplace_back(TokenType::kPlus,      "+", line_); break;
                case '-': tokens.emplace_back(TokenType::kMinus,     "-", line_); break;
                case '*': tokens.emplace_back(TokenType::kStar,      "*", line_); break;
                case '/': tokens.emplace_back(TokenType::kSlash,     "/", line_); break;
                case '%': tokens.emplace_back(TokenType::kPercent,   "%", line_); break;
                case '=': tokens.emplace_back(TokenType::kEquals,    "=", line_); break;
                default:  tokens.emplace_back(TokenType::kUnknown,   std::string(1, c), line_); break;
            }
        }
    }

    return tokens;
}
