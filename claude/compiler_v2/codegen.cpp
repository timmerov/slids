#include "codegen.h"

#include <cassert>
#include <map>
#include <ostream>
#include <sstream>
#include <string>

#include "ast.h"
#include "diagnostic.h"
#include "print.h"
#include "strings.h"

namespace codegen {

namespace {

struct VarInfo {
    std::string alloca_name;   // e.g. "%ch"
    std::string llvm_type;     // e.g. "i8", "i32", "ptr"
};

using SymTab = std::map<std::string, VarInfo>;

std::string llvmTypeFor(std::string const& slids_type, diagnostic::Sink& diag) {
    if (slids_type == "int32")  return "i32";
    if (slids_type == "char")   return "i8";
    if (slids_type == "char[]") return "ptr";
    diagnostic::report(diag, {-1, -1,
        "codegen: unknown type '" + slids_type + "'", {}});
    return "i32";
}

std::string emitExpr(ast::Node const& expr, SymTab const& syms,
                     strings::Pool& pool, std::ostream& out,
                     diagnostic::Sink& diag) {
    (void)syms;
    (void)out;
    switch (expr.kind) {
        case ast::Kind::kIntLiteral:
            return expr.text;
        case ast::Kind::kCharLiteral:
            return expr.text;
        case ast::Kind::kStringLiteral: {
            int id = strings::add(pool, expr.text);
            return std::string("@.str_") + std::to_string(id);
        }
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kVarDeclStmt:
        case ast::Kind::kAssignStmt:
        case ast::Kind::kCallStmt:
        case ast::Kind::kReturnStmt:
            diagnostic::report(diag, {-1, -1,
                "codegen: not an expression", {}});
            return "0";
    }
    assert(false && "emitExpr: unhandled ast::Kind");
    __builtin_unreachable();
}

void emitStmt(ast::Node const& stmt, SymTab& syms,
              strings::Pool& pool, print::CallStrings const& cs,
              std::ostream& out, diagnostic::Sink& diag) {
    switch (stmt.kind) {
        case ast::Kind::kVarDeclStmt: {
            std::string llty = llvmTypeFor(stmt.return_type, diag);
            std::string regname = std::string("%") + stmt.name;
            out << "  " << regname << " = alloca " << llty << "\n";
            syms[stmt.name] = {regname, llty};
            return;
        }
        case ast::Kind::kAssignStmt: {
            auto it = syms.find(stmt.name);
            if (it == syms.end()) {
                diagnostic::report(diag, {-1, -1,
                    "codegen: unknown variable '" + stmt.name + "'", {}});
                return;
            }
            std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag);
            out << "  store " << it->second.llvm_type << " " << val
                << ", ptr " << it->second.alloca_name << "\n";
            return;
        }
        case ast::Kind::kCallStmt:
            if (!print::tryEmitCall(stmt, cs, out)) {
                diagnostic::report(diag, {-1, -1,
                    "codegen: unknown call '" + stmt.name + "'", {}});
            }
            return;
        case ast::Kind::kReturnStmt:
            out << "  ret i32 " << stmt.children[0]->text << "\n";
            return;
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kStringLiteral:
        case ast::Kind::kIntLiteral:
        case ast::Kind::kCharLiteral:
            diagnostic::report(diag, {-1, -1,
                "codegen: unexpected node kind in statement position", {}});
            return;
    }
    assert(false && "emitStmt: unhandled ast::Kind");
    __builtin_unreachable();
}

void emitFunction(ast::Node const& fn, strings::Pool& pool,
                  print::CallStrings const& cs,
                  std::ostream& out, diagnostic::Sink& diag) {
    out << "define i32 @" << fn.name << "() {\n";
    SymTab syms;
    for (auto const& s : fn.children) {
        emitStmt(*s, syms, pool, cs, out, diag);
    }
    out << "}\n";
}

}  // namespace

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag) {
    strings::Pool pool;
    print::CallStrings cs = print::collect(tree, pool);

    // Buffer function bodies so any string literals encountered during emit
    // (from assignments etc.) get registered in the pool before we flush.
    std::ostringstream body;
    for (auto const& n : tree.nodes) {
        if (n->kind != ast::Kind::kProgram) continue;
        for (auto const& fn : n->children) {
            if (fn->kind == ast::Kind::kFunctionDef) {
                emitFunction(*fn, pool, cs, body, diag);
            } else if (fn->kind == ast::Kind::kFunctionDecl) {
                // intentional n/a: declarations carry no body to emit
            } else {
                diagnostic::report(diag, {-1, -1,
                    "codegen: unexpected node at program scope", {}});
            }
        }
    }

    out << "target triple = \"x86_64-pc-linux-gnu\"\n\n";
    strings::emit(pool, out);
    if (!pool.texts.empty()) out << "\n";
    out << "declare i32 @printf(ptr, ...)\n\n";
    out << body.str();
}

}  // namespace codegen
