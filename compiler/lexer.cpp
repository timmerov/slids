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
                // single line comment
                while (pos_ < (int)source_.size() && peek() != '\n')
                    advance();
            } else if (source_[pos_ + 1] == '*') {
                // multi-line comment
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
    if (peek() == '"') advance(); // consume closing "
    return Token(TokenType::kStringLiteral, value, line_);
}

Token Lexer::readNumber() {
    std::string value;
    while (pos_ < (int)source_.size() && (isdigit(peek()) || peek() == '_'))
        value += advance();
    // strip underscores
    std::string clean;
    for (char c : value)
        if (c != '_') clean += c;
    return Token(TokenType::kIntLiteral, clean, line_);
}

Token Lexer::readIdentifierOrKeyword() {
    std::string value;
    while (pos_ < (int)source_.size() && (isalnum(peek()) || peek() == '_'))
        value += advance();

    if (value == "int32")  return Token(TokenType::kInt32,  value, line_);
    if (value == "void")   return Token(TokenType::kVoid,   value, line_);
    if (value == "return") return Token(TokenType::kReturn, value, line_);

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

        if (c == '"')               { tokens.push_back(readString()); }
        else if (isdigit(c))        { tokens.push_back(readNumber()); }
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
                default:  tokens.emplace_back(TokenType::kUnknown,   std::string(1, c), line_); break;
            }
        }
    }

    return tokens;
}
