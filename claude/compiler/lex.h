#pragma once

#include <deque>
#include <string>
#include <vector>

#include "token.h"

namespace diagnostic { struct Sink; }

namespace lex {

struct Stream {
    int file_id = -1;
    std::string const* source = nullptr;
    int pos = 0;
    int line = 1;
    int col = 1;
    bool fatal = false;
    std::string fatal_msg;
    int fatal_line = 0;
    int fatal_col = 0;
    int fatal_length = 0;
    std::deque<token::Token> pending;   // for runs that lex into multiple tokens (e.g. `^^^`)
};

Stream open(int file_id, std::string const& source);
token::Token next(Stream& s);   // returns kEndOfFile at end; kError on fatal (msg in s.fatal_msg)

void run(std::string const& root_path,
         std::vector<std::string> const& import_paths,
         token::List& out, diagnostic::Sink& diag);

}  // namespace lex
