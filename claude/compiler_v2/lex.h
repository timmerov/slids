#pragma once

#include <string>

namespace token { struct List; }
namespace diagnostic { struct Sink; }

namespace lex {

void run(std::string const& source, token::List& out, diagnostic::Sink& diag);

}  // namespace lex
