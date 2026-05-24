#pragma once

#include <memory>
#include <vector>

namespace ast {

struct Node {
    int kind;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

void addProgram(Tree& tree);

}  // namespace ast
