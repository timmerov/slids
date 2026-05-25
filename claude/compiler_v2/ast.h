#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ast {

enum class Kind {
    kProgram,
    kFunctionDef,
    kFunctionDecl,
    kVarDeclStmt,
    kAssignStmt,
    kCallStmt,
    kReturnStmt,
    kStringLiteral,
    kIntLiteral,
    kCharLiteral,
    kBoolLiteral,
    kFloatLiteral,
    kIdentExpr,
    kUnaryExpr,
    kBinaryExpr,
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
