#pragma once

#include <vector>

namespace ast {

struct Node;

struct Tree {
    std::vector<Node*> nodes;
};

}  // namespace ast
