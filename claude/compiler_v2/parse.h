#pragma once

#include <vector>

namespace parse {

struct Node;

struct Tree {
    std::vector<Node*> nodes;
};

}  // namespace parse
