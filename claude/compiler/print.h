#pragma once

#include <iosfwd>

#include "codegen.h"

namespace ast { struct Node; }
namespace diagnostic { struct Sink; }
namespace strings { struct Pool; }

namespace print {

// Emits a __println / __print intrinsic call. Flattens any left-leaning '+'
// chain in the single argument into segments, classifies each segment via
// codegen::exprType, builds a composite printf format string, and emits one
// printf with all collected varargs. Returns false if the callee name isn't
// a print intrinsic.
bool tryEmitCall(ast::Node const& call_node, codegen::SymTab const& syms,
                 strings::Pool& pool, std::ostream& out,
                 diagnostic::Sink& diag);

}  // namespace print
