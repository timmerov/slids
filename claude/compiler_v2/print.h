#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace ast { struct Node; struct Tree; }

namespace print {

struct State {
    std::map<ast::Node const*, int> call_to_str_id;
    std::vector<std::string> str_texts;
};

State collect(ast::Tree const& tree);
void emitConstants(State const& state, std::ostream& out);
bool tryEmitCall(ast::Node const& call_node, State const& state, std::ostream& out);

}  // namespace print
