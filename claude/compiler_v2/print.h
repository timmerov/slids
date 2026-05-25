#pragma once

#include <iosfwd>
#include <map>

namespace ast { struct Node; struct Tree; }
namespace strings { struct Pool; }

namespace print {

struct CallStrings {
    std::map<ast::Node const*, int> call_to_str_id;
};

CallStrings collect(ast::Tree const& tree, strings::Pool& pool);
bool tryEmitCall(ast::Node const& call_node, CallStrings const& cs, std::ostream& out);

}  // namespace print
