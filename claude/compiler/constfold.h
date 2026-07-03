#pragma once

namespace parse { struct Tree; }
namespace diagnostic { struct Sink; }

namespace constfold {

// Walk the parse tree post-order: assign nominal_type to every literal,
// and fold unary/binary expressions whose operands are literals.
// Spec: test_v2/const/fold.sl.
void run(parse::Tree& tree, diagnostic::Sink& diag);

}  // namespace constfold
