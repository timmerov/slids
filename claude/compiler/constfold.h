#pragma once

namespace parse { struct Tree; struct Node; }
namespace diagnostic { struct Sink; }

namespace constfold {

// Walk the parse tree post-order: assign nominal_type to every literal,
// and fold unary/binary expressions whose operands are literals.
// Spec: test_v2/const/fold.sl.
void run(parse::Tree& tree, diagnostic::Sink& diag);

// Fold ONE function subtree — a template instance resolved after the program-wide
// run (classify instantiates on demand, then folds the clone through this).
void runOn(parse::Tree& tree, parse::Node& fn, diagnostic::Sink& diag);

}  // namespace constfold
