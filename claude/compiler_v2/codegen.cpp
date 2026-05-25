#include "codegen.h"

#include <cassert>
#include <ostream>
#include <string>

#include "ast.h"
#include "diagnostic.h"
#include "print.h"

namespace codegen {

namespace {

void emitStmt(ast::Node const& stmt, print::State const& strings,
              std::ostream& out, diagnostic::Sink& diag) {
    switch (stmt.kind) {
        case ast::Kind::kCallStmt:
            if (!print::tryEmitCall(stmt, strings, out)) {
                diagnostic::report(diag, {-1, -1,
                    "codegen: unknown call '" + stmt.name + "'", {}});
            }
            return;
        case ast::Kind::kReturnStmt:
            out << "  ret i32 " << stmt.children[0]->text << "\n";
            return;
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kStringLiteral:
        case ast::Kind::kIntLiteral:
            diagnostic::report(diag, {-1, -1,
                "codegen: unexpected node kind in statement position", {}});
            return;
    }
    assert(false && "emitStmt: unhandled ast::Kind");
    __builtin_unreachable();
}

void emitFunction(ast::Node const& fn, print::State const& strings,
                  std::ostream& out, diagnostic::Sink& diag) {
    out << "define i32 @" << fn.name << "() {\n";
    for (auto const& s : fn.children) {
        emitStmt(*s, strings, out, diag);
    }
    out << "}\n";
}

}  // namespace

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag) {
    out << "target triple = \"x86_64-pc-linux-gnu\"\n\n";
    print::State strings = print::collect(tree);
    print::emitConstants(strings, out);
    for (auto const& n : tree.nodes) {
        if (n->kind != ast::Kind::kProgram) continue;
        for (auto const& fn : n->children) {
            if (fn->kind == ast::Kind::kFunctionDef) {
                emitFunction(*fn, strings, out, diag);
            }
        }
    }
}

}  // namespace codegen
