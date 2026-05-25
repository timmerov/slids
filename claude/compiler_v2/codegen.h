#pragma once

#include <iosfwd>
#include <map>
#include <string>

namespace ast { struct Tree; }
namespace diagnostic { struct Sink; }

namespace codegen {

struct VarInfo {
    std::string alloca_name;   // e.g. "%ch"
    std::string llvm_type;     // e.g. "i8", "i32", "ptr"
};

using SymTab = std::map<std::string, VarInfo>;

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag);

}  // namespace codegen
