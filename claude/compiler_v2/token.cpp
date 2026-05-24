#include "token.h"

namespace token {

void add(List& list, Token const& tok) {
    list.tokens.push_back(tok);
}

}  // namespace token
