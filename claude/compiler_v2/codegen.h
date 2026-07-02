#pragma once

#include <iosfwd>
#include <map>
#include <string>

#include "widen.h"   // widen::TypeRef

namespace ast { struct Node; struct Tree; }
namespace diagnostic { struct Sink; }
namespace strings { struct Pool; }

namespace codegen {

struct VarInfo {
    std::string alloca_name;       // storage address: "%ch" (a local alloca) OR
                                   // "@__global_x" (a global's symbol) — both `ptr`,
                                   // so load/store/GEP treat them identically.
    std::string llvm_type;         // e.g. "i8", "i32", "ptr"
    widen::TypeRef slids_type;     // structured type handle — for widening checks
    std::string touch_symbol = {}; // "" for a local / static global; else a LAZY
                                   // global's first-touch thunk, called before access.
};

// Keyed by parse::Tree::entries index — every ident / lvalue node carries its
// resolved_entry_id pre-stamped by classify, so codegen does no string lookup.
using SymTab = std::map<int, VarInfo>;

// Emits LLVM IR for an expression, returning the value name (register or
// literal). dest_type drives literal range checks and var-to-var widening.
// Exposed so print.cpp can drive per-segment value emission.
std::string emitExpr(ast::Node const& expr, SymTab const& syms,
                     strings::Pool& pool, std::ostream& out,
                     diagnostic::Sink& diag,
                     widen::TypeRef dest_type);

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag);

}  // namespace codegen
