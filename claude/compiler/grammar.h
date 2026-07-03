#pragma once

namespace token { struct List; }
namespace parse { struct Tree; }
namespace diagnostic { struct Sink; }

namespace grammar {

void run(token::List const& in, parse::Tree& out, diagnostic::Sink& diag);

}  // namespace grammar
