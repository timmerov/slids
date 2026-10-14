#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

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
    bool indirect = false;         // a RUNTIME-SIZED global: alloca_name is a POINTER
                                   // SLOT, not the object — the object is on the heap
                                   // (built by the touch thunk); load the slot for its
                                   // address. Only emitVarAddr reads this.
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

// How `run` emits this module.
struct Options {
    // --instantiate-self: emit ONLY this TU's INLINE-LOCAL template flavors — the
    // flavors whose type-list names a type private to this TU, which no other TU can
    // spell and which the main compile therefore cannot leave to a library's
    // --instantiate pass. Every OTHER definition becomes a `declare` (or an `external
    // global`), so the module is valid IR on its own and llvm-link resolves the
    // references against the main module. The flavors are emitted with EXTERNAL
    // linkage, because an `internal` definition in one module does not satisfy
    // another's `declare` — llvm-link drops it, silently. The internalize pass that
    // follows the merge demotes them again, driven by `api_symbols`.
    bool self_instantiate = false;
    // --api-list: when set, every symbol this module DEFINES with external linkage is
    // appended here — the allow-list `opt -passes=internalize` needs, since anything
    // absent from it is demoted. `main` is not preserved automatically, and neither are
    // globals, so the list has to be complete; an omission is an undefined reference at
    // link, never a silent miscompile.
    std::vector<std::string>* api_symbols = nullptr;
};

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag,
         Options const& opts = {});

}  // namespace codegen
