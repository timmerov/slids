#pragma once

#include <memory>
#include <vector>

namespace parse {

struct Node {
    int kind;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

void addProgram(Tree& tree);

}  // namespace parse
