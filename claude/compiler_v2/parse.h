#pragma once

#include <memory>
#include <string>
#include <vector>

namespace parse {

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
    kUnaryExpr,    // text = op ("+", "-", "!", "~"); children[0] = operand
    kBinaryExpr,   // text = op (e.g. "+", "<<", "&&"); children[0] = lhs, [1] = rhs
};

struct Node {
    Kind kind;
    std::string name;          // function name, callee name, variable name
    std::string text;          // literal value (string / int as text / char codepoint)
    std::string return_type;   // function return type; reused for VarDecl's declared type
    std::vector<std::unique_ptr<Node>> children;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

}  // namespace parse
