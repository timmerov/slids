#pragma once
#include "token.h"
#include <string>
#include <vector>

class SourceMap;

class Lexer {
public:
    Lexer(SourceMap& sm, int file_id);
    std::vector<Token> tokenize();

private:
    SourceMap& sm_;
    int file_id_;
    const std::string& source_;
    int pos_;
    int line_;
    int col_;

    char peek();
    char peek2();
    char advance();
    void skipWhitespaceAndComments();
    Token readString();
    Token readCharLiteral();
    Token readNumber();
    Token readIdentifierOrKeyword();
    [[noreturn]] void throwLexError(int line, int col, int length, const std::string& msg);
};
