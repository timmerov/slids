#pragma once

#include <memory>
#include <string>
#include <vector>

namespace parse {

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
    std::string name;          // function name, callee name
    std::string text;          // literal value (string / int as text)
    std::string return_type;   // function return type (text)
    std::vector<std::unique_ptr<Node>> children;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

}  // namespace parse
