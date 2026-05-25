#include "desugar.h"

#include <cassert>
#include <memory>

#include "ast.h"
#include "diagnostic.h"
#include "parse.h"

namespace desugar {

namespace {

ast::Kind toAstKind(parse::Kind k) {
    switch (k) {
        case parse::Kind::kProgram:       return ast::Kind::kProgram;
        case parse::Kind::kFunctionDef:   return ast::Kind::kFunctionDef;
        case parse::Kind::kCallStmt:      return ast::Kind::kCallStmt;
        case parse::Kind::kReturnStmt:    return ast::Kind::kReturnStmt;
        case parse::Kind::kStringLiteral: return ast::Kind::kStringLiteral;
        case parse::Kind::kIntLiteral:    return ast::Kind::kIntLiteral;
    }
    assert(false && "toAstKind: unhandled parse::Kind");
    __builtin_unreachable();
}

std::unique_ptr<ast::Node> copyNode(parse::Node const& p) {
    auto node = std::make_unique<ast::Node>();
    node->kind = toAstKind(p.kind);
    node->name = p.name;
    node->text = p.text;
    node->return_type = p.return_type;
    for (auto const& c : p.children) {
        node->children.push_back(copyNode(*c));
    }
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
