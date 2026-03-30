#pragma once
#include "token.h"
#include <string>
#include <vector>

class Lexer {
public:
    Lexer(const std::string& source);
    std::vector<Token> tokenize();

private:
    std::string source_;
    int pos_;
    int line_;

    char peek();
    char peek2();
    char advance();
    void skipWhitespaceAndComments();
    Token readString();
    Token readNumber();
    Token readIdentifierOrKeyword();
};
