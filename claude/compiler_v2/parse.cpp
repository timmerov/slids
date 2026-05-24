#include "parse.h"

#include <utility>

namespace parse {

void addProgram(Tree& tree) {
    auto node = std::make_unique<Node>();
    node->kind = 1;   // kProgram
    tree.nodes.push_back(std::move(node));
}

}  // namespace parse
