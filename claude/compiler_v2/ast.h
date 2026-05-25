#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ast {

enum class Kind {
    kProgram,
    kFunctionDef,
    kCallStmt,
    kReturnStmt,
    kStringLiteral,
    kIntLiteral,
};

struct Node {
    Kind kind;
    std::string name;
    std::string text;
    std::string return_type;
    std::vector<std::unique_ptr<Node>> children;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

}  // namespace ast
