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
    kAugAssignStmt,
    kCallStmt,
    kReturnStmt,
    kStringLiteral,
    kIntLiteral,
    kUintLiteral,
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
    int file_id = -1;          // source file of the construct
    int tok = -1;              // index into token::List::tokens for error attribution
    std::vector<std::unique_ptr<Node>> children;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

}  // namespace ast
