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
    std::string nominal_type;    // literal nodes: nominal type from constfold
    std::string inferred_type;   // expression nodes: in-context type from classify
    std::string op_type;         // binary's computational type from classify
    int file_id = -1;            // source file of the construct
    int tok = -1;                // index into token::List::tokens for error attribution
    int resolved_entry_id = -1;  // ident / lhs / callee -> parse::Tree::entries index
    std::vector<std::unique_ptr<Node>> children;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

}  // namespace ast
