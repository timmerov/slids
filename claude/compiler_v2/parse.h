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
    kAugAssignStmt,  // name = lhs, text = op (e.g. "+", "&&"); children[0] = rhs
    kCallStmt,
    kReturnStmt,
    kStringLiteral,
    kIntLiteral,
    kUintLiteral,
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
    std::string nominal_type;  // literal nodes: nominal type assigned by constfold
    int file_id = -1;          // source file of the construct
    int tok = -1;              // index into token::List::tokens for error attribution
    std::vector<std::unique_ptr<Node>> children;
};

struct Tree {
    std::vector<std::unique_ptr<Node>> nodes;
};

}  // namespace parse
