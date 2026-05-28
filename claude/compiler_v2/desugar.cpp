#include "desugar.h"

#include <cassert>
#include <memory>
#include <string>

#include "ast.h"
#include "diagnostic.h"
#include "parse.h"

namespace desugar {

namespace {

ast::Kind toAstKind(parse::Kind k) {
    switch (k) {
        case parse::Kind::kProgram:       return ast::Kind::kProgram;
        case parse::Kind::kFunctionDef:   return ast::Kind::kFunctionDef;
        case parse::Kind::kFunctionDecl:  return ast::Kind::kFunctionDecl;
        case parse::Kind::kVarDeclStmt:   return ast::Kind::kVarDeclStmt;
        case parse::Kind::kAssignStmt:    return ast::Kind::kAssignStmt;
        case parse::Kind::kAugAssignStmt: return ast::Kind::kAugAssignStmt;
        case parse::Kind::kCallStmt:      return ast::Kind::kCallStmt;
        case parse::Kind::kReturnStmt:    return ast::Kind::kReturnStmt;
        case parse::Kind::kStringLiteral: return ast::Kind::kStringLiteral;
        case parse::Kind::kIntLiteral:    return ast::Kind::kIntLiteral;
        case parse::Kind::kUintLiteral:   return ast::Kind::kUintLiteral;
        case parse::Kind::kCharLiteral:   return ast::Kind::kCharLiteral;
        case parse::Kind::kBoolLiteral:   return ast::Kind::kBoolLiteral;
        case parse::Kind::kFloatLiteral:  return ast::Kind::kFloatLiteral;
        case parse::Kind::kIdentExpr:     return ast::Kind::kIdentExpr;
        case parse::Kind::kUnaryExpr:     return ast::Kind::kUnaryExpr;
        case parse::Kind::kBinaryExpr:    return ast::Kind::kBinaryExpr;
    }
    assert(false && "toAstKind: unhandled parse::Kind");
    __builtin_unreachable();
}

// Rewrite `lhs op= rhs;` into `lhs = lhs op rhs;`. Only fires when `node` is
// a kAugAssignStmt. Lvalue is a bare ident today — when complex lvalues land
// (`arr[f()] += 1`), bind the lhs to a tmp here to avoid double-evaluation.
std::unique_ptr<ast::Node> tryDesugarAugAssign(ast::Node& node) {
    if (node.kind != ast::Kind::kAugAssignStmt) return nullptr;
    assert(node.children.size() == 1
        && "tryDesugarAugAssign: AugAssignStmt needs 1 rhs child");

    auto lhs_ref = std::make_unique<ast::Node>();
    lhs_ref->kind = ast::Kind::kIdentExpr;
    lhs_ref->name = node.name;
    lhs_ref->file_id = node.file_id;
    lhs_ref->tok = node.tok;

    auto binop = std::make_unique<ast::Node>();
    binop->kind = ast::Kind::kBinaryExpr;
    binop->text = node.text;
    binop->file_id = node.file_id;
    binop->tok = node.tok;
    binop->children.push_back(std::move(lhs_ref));
    binop->children.push_back(std::move(node.children[0]));

    auto out = std::make_unique<ast::Node>();
    out->kind = ast::Kind::kAssignStmt;
    out->name = std::move(node.name);
    out->file_id = node.file_id;
    out->tok = node.tok;
    out->children.push_back(std::move(binop));
    return out;
}

std::unique_ptr<ast::Node> copyNode(parse::Node const& p) {
    auto node = std::make_unique<ast::Node>();
    node->kind = toAstKind(p.kind);
    node->name = p.name;
    node->text = p.text;
    node->return_type = p.return_type;
    node->nominal_type = p.nominal_type;
    node->file_id = p.file_id;
    node->tok = p.tok;
    for (auto const& c : p.children) {
        node->children.push_back(copyNode(*c));
    }
    if (auto rewritten = tryDesugarAugAssign(*node)) return rewritten;
    return node;
}

}  // namespace

void run(parse::Tree const& in, ast::Tree& out, diagnostic::Sink& diag) {
    (void)diag;
    for (auto const& n : in.nodes) {
        out.nodes.push_back(copyNode(*n));
    }
}

}  // namespace desugar
