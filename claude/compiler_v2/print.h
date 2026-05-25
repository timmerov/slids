#pragma once

#include <iosfwd>
#include <map>

#include "codegen.h"

namespace ast { struct Node; struct Tree; }
namespace strings { struct Pool; }

namespace print {

struct CallStrings {
    std::map<ast::Node const*, int> call_to_str_id;   // string-literal arg → str id
    std::map<ast::Node const*, int> call_to_fmt_id;   // ident arg → fmt str id
};

CallStrings collect(ast::Tree const& tree, strings::Pool& pool);
bool tryEmitCall(ast::Node const& call_node, CallStrings const& cs,
                 codegen::SymTab const& syms, std::ostream& out);

}  // namespace print
