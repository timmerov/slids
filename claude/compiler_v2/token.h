#pragma once

#include <string>
#include <vector>

namespace token {

struct Token {
    int kind;
    std::string text;
    int line;
    int col;
};

struct List {
    std::vector<Token> tokens;
};

void add(List& list, Token const& tok);

}  // namespace token
