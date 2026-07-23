#pragma once

namespace token { struct List; }
namespace parse { struct Tree; }
namespace diagnostic { struct Sink; }

namespace grammar {

// `in` is mutable for ONE reason: the `>>` closer split — a nested template
// type use (`Vec<Vec<int>>`) rewrites the right-shift token into two `>`s
// in place (parse is a single forward pass, so recorded indices stay valid).
void run(token::List& in, parse::Tree& out, diagnostic::Sink& diag);

}  // namespace grammar
