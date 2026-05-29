#pragma once

#include <iosfwd>
#include <map>
#include <string>

namespace ast { struct Node; struct Tree; }
namespace diagnostic { struct Sink; }
namespace strings { struct Pool; }

namespace codegen {

struct VarInfo {
    std::string alloca_name;   // e.g. "%ch"
    std::string llvm_type;     // e.g. "i8", "i32", "ptr"
    std::string slids_type;    // e.g. "char", "int32", "char[]" — for widening checks
};

using SymTab = std::map<std::string, VarInfo>;

// Emits LLVM IR for an expression, returning the value name (register or
// literal). dest_type drives literal range checks and var-to-var widening.
// Exposed so print.cpp can drive per-segment value emission.
std::string emitExpr(ast::Node const& expr, SymTab const& syms,
                     strings::Pool& pool, std::ostream& out,
                     diagnostic::Sink& diag,
                     std::string const& dest_type);

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag);

}  // namespace codegen
