#include "codegen.h"

#include <cassert>
#include <ostream>
#include <sstream>
#include <string>

#include "ast.h"
#include "diagnostic.h"
#include "print.h"
#include "strings.h"
#include "widen.h"

namespace codegen {

// Forward decl — defined below, outside the anonymous namespace, so print.cpp
// can call it via codegen::emitExpr; the anon-namespace helpers (emitUnary /
// emitBinary) call this same symbol unqualified.
std::string emitExpr(ast::Node const& expr, SymTab const& syms,
                     strings::Pool& pool, std::ostream& out,
                     diagnostic::Sink& diag,
                     std::string const& dest_type);

namespace {

std::string llvmTypeFor(std::string const& slids_type, diagnostic::Sink& diag) {
    if (slids_type == "bool")    return "i1";
    if (slids_type == "char"
     || slids_type == "int8"
     || slids_type == "uint8")   return "i8";
    if (slids_type == "int16"
     || slids_type == "uint16")  return "i16";
    if (slids_type == "int"
     || slids_type == "int32"
     || slids_type == "uint"
     || slids_type == "uint32")  return "i32";
    if (slids_type == "int64"
     || slids_type == "uint64"
     || slids_type == "intptr")  return "i64";
    if (slids_type == "float"
     || slids_type == "float32") return "float";
    if (slids_type == "float64") return "double";
    if (slids_type == "void")    return "void";
    if (slids_type.size() >= 2 && slids_type.substr(slids_type.size() - 2) == "[]")
        return "ptr";
    diagnostic::report(diag, {-1, -1,
        "codegen: unknown type '" + slids_type + "'", {}});
    return "i32";
}

std::string normalizeFloatLiteral(std::string const& text) {
    if (text.find('.') != std::string::npos) return text;
    auto e = text.find_first_of("eE");
    std::string out = text;
    if (e == std::string::npos) out += ".0";
    else out.insert(e, ".0");
    return out;
}

std::string newTmp(char const* tag) {
    static int n = 0;
    return std::string("%") + tag + "_" + std::to_string(n++);
}

bool isFloatType(std::string const& t) {
    return t == "float" || t == "float32" || t == "float64";
}

bool isUnsignedType(std::string const& t) {
    return t == "uint" || t == "uint8" || t == "uint16" || t == "uint32"
        || t == "uint64" || t == "char";
}

std::string emitUnary(ast::Node const& expr, SymTab const& syms,
                      strings::Pool& pool, std::ostream& out,
                      diagnostic::Sink& diag,
                      std::string const& dest_type) {
    std::string const& op = expr.text;
    ast::Node const& operand = *expr.children[0];

    if (op == "+") {
        return emitExpr(operand, syms, pool, out, diag, dest_type);
    }
    if (op == "-") {
        std::string v = emitExpr(operand, syms, pool, out, diag, dest_type);
        std::string llty = llvmTypeFor(dest_type, diag);
        std::string tmp = newTmp("neg");
        out << "  " << tmp << " = sub " << llty << " 0, " << v << "\n";
        return tmp;
    }
    if (op == "~") {
        std::string v = emitExpr(operand, syms, pool, out, diag, dest_type);
        std::string llty = llvmTypeFor(dest_type, diag);
        std::string tmp = newTmp("bnot");
        out << "  " << tmp << " = xor " << llty << " " << v << ", -1\n";
        return tmp;
    }
    if (op == "!") {
        std::string operand_type = exprType(operand, syms);
        if (operand_type.empty()) operand_type = "int";
        std::string v = emitExpr(operand, syms, pool, out, diag, operand_type);
        std::string llty = llvmTypeFor(operand_type, diag);
        std::string tmp = newTmp("lnot");
        // 0-like → true, else false. Integer-truthiness: icmp eq 0.
        out << "  " << tmp << " = icmp eq " << llty << " " << v << ", 0\n";
        return tmp;
    }
    diagnostic::report(diag, {-1, -1,
        "codegen: unary operator '" + op + "' not yet supported", {}});
    return "0";
}

std::string emitBinary(ast::Node const& expr, SymTab const& syms,
                       strings::Pool& pool, std::ostream& out,
                       diagnostic::Sink& diag,
                       std::string const& dest_type) {
    std::string const& op = expr.text;
    ast::Node const& lhs = *expr.children[0];
    ast::Node const& rhs = *expr.children[1];

    // Comparisons: result type is bool (i1); operands share their natural type.
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        std::string lt = exprType(lhs, syms);
        std::string rt = exprType(rhs, syms);
        std::string opty = !lt.empty() ? lt : (!rt.empty() ? rt : std::string("int"));
        std::string lv = emitExpr(lhs, syms, pool, out, diag, opty);
        std::string rv = emitExpr(rhs, syms, pool, out, diag, opty);
        std::string llty = llvmTypeFor(opty, diag);
        bool flt = isFloatType(opty);
        bool uns = isUnsignedType(opty) || opty == "bool";
        char const* pred =
              op == "==" ? (flt ? "oeq" : "eq")
            : op == "!=" ? (flt ? "one" : "ne")
            : op == "<"  ? (flt ? "olt" : (uns ? "ult" : "slt"))
            : op == "<=" ? (flt ? "ole" : (uns ? "ule" : "sle"))
            : op == ">"  ? (flt ? "ogt" : (uns ? "ugt" : "sgt"))
                          : (flt ? "oge" : (uns ? "uge" : "sge"));
        std::string tmp = newTmp("cmp");
        out << "  " << tmp << " = " << (flt ? "fcmp " : "icmp ")
            << pred << " " << llty << " " << lv << ", " << rv << "\n";
        return tmp;
    }

    // Arithmetic / bitwise / shift — both operands at dest_type.
    std::string lv = emitExpr(lhs, syms, pool, out, diag, dest_type);
    std::string rv = emitExpr(rhs, syms, pool, out, diag, dest_type);
    std::string llty = llvmTypeFor(dest_type, diag);
    bool flt = isFloatType(dest_type);
    bool uns = isUnsignedType(dest_type);

    std::string instr;
    if (flt) {
        if (op == "+") instr = "fadd";
        else if (op == "-") instr = "fsub";
        else if (op == "*") instr = "fmul";
        else if (op == "/") instr = "fdiv";
    } else {
        if      (op == "+")  instr = "add";
        else if (op == "-")  instr = "sub";
        else if (op == "*")  instr = "mul";
        else if (op == "/")  instr = uns ? "udiv" : "sdiv";
        else if (op == "%")  instr = uns ? "urem" : "srem";
        else if (op == "&")  instr = "and";
        else if (op == "|")  instr = "or";
        else if (op == "^")  instr = "xor";
        else if (op == "<<") instr = "shl";
        else if (op == ">>") instr = uns ? "lshr" : "ashr";
    }
    if (instr.empty()) {
        diagnostic::report(diag, {-1, -1,
            "codegen: binary operator '" + op + "' on '" + dest_type
            + "' not yet supported", {}});
        return "0";
    }
    std::string tmp = newTmp("bin");
    out << "  " << tmp << " = " << instr << " " << llty
        << " " << lv << ", " << rv << "\n";
    return tmp;
}

}  // namespace

std::string emitExpr(ast::Node const& expr, SymTab const& syms,
                     strings::Pool& pool, std::ostream& out,
                     diagnostic::Sink& diag,
                     std::string const& dest_type) {
    switch (expr.kind) {
        case ast::Kind::kIntLiteral:
            widen::checkIntLiteralFits(expr.text, dest_type, diag);
            return expr.text;
        case ast::Kind::kCharLiteral:
            widen::checkIntLiteralFits(expr.text, dest_type, diag);
            return expr.text;
        case ast::Kind::kBoolLiteral: {
            std::string v = (expr.text == "true") ? "1" : "0";
            widen::checkIntLiteralFits(v, dest_type, diag);
            return v;
        }
        case ast::Kind::kFloatLiteral: {
            widen::checkFloatLiteralFits(expr.text, dest_type, diag);
            return normalizeFloatLiteral(expr.text);
        }
        case ast::Kind::kStringLiteral: {
            int id = strings::add(pool, expr.text);
            return std::string("@.str_") + std::to_string(id);
        }
        case ast::Kind::kIdentExpr: {
            auto it = syms.find(expr.name);
            if (it == syms.end()) {
                diagnostic::report(diag, {-1, -1,
                    "codegen: unknown variable '" + expr.name + "'", {}});
                return "0";
            }
            std::string tmp = newTmp("ld");
            out << "  " << tmp << " = load " << it->second.llvm_type
                << ", ptr " << it->second.alloca_name << "\n";
            return widen::convert(tmp, it->second.slids_type, dest_type, out, diag);
        }
        case ast::Kind::kUnaryExpr:
            return emitUnary(expr, syms, pool, out, diag, dest_type);
        case ast::Kind::kBinaryExpr:
            return emitBinary(expr, syms, pool, out, diag, dest_type);
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

namespace {

void emitStmt(ast::Node const& stmt, SymTab& syms,
              strings::Pool& pool,
              std::string const& fn_return_type,
              std::ostream& out, diagnostic::Sink& diag) {
    switch (stmt.kind) {
        case ast::Kind::kVarDeclStmt: {
            std::string llty = llvmTypeFor(stmt.return_type, diag);
            std::string regname = std::string("%") + stmt.name;
            out << "  " << regname << " = alloca " << llty << "\n";
            syms[stmt.name] = {regname, llty, stmt.return_type};
            if (!stmt.children.empty()) {
                std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                           stmt.return_type);
                out << "  store " << llty << " " << val
                    << ", ptr " << regname << "\n";
            }
            return;
        }
        case ast::Kind::kAssignStmt: {
            auto it = syms.find(stmt.name);
            if (it == syms.end()) {
                diagnostic::report(diag, {-1, -1,
                    "codegen: unknown variable '" + stmt.name + "'", {}});
                return;
            }
            std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                       it->second.slids_type);
            out << "  store " << it->second.llvm_type << " " << val
                << ", ptr " << it->second.alloca_name << "\n";
            return;
        }
        case ast::Kind::kCallStmt:
            if (!print::tryEmitCall(stmt, syms, pool, out, diag)) {
                diagnostic::report(diag, {-1, -1,
                    "codegen: unknown call '" + stmt.name + "'", {}});
            }
            return;
        case ast::Kind::kReturnStmt: {
            std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                       fn_return_type);
            std::string llty = llvmTypeFor(fn_return_type, diag);
            out << "  ret " << llty << " " << val << "\n";
            return;
        }
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kStringLiteral:
        case ast::Kind::kIntLiteral:
        case ast::Kind::kCharLiteral:
        case ast::Kind::kBoolLiteral:
        case ast::Kind::kFloatLiteral:
        case ast::Kind::kIdentExpr:
        case ast::Kind::kUnaryExpr:
        case ast::Kind::kBinaryExpr:
            diagnostic::report(diag, {-1, -1,
                "codegen: unexpected node kind in statement position", {}});
            return;
    }
    assert(false && "emitStmt: unhandled ast::Kind");
    __builtin_unreachable();
}

void emitFunction(ast::Node const& fn, strings::Pool& pool,
                  std::ostream& out, diagnostic::Sink& diag) {
    std::string ret_llty = llvmTypeFor(fn.return_type, diag);
    out << "define " << ret_llty << " @" << fn.name << "() {\n";
    SymTab syms;
    for (auto const& s : fn.children) {
        emitStmt(*s, syms, pool, fn.return_type, out, diag);
    }
    out << "}\n";
}

}  // namespace

std::string exprType(ast::Node const& expr, SymTab const& syms) {
    switch (expr.kind) {
        case ast::Kind::kIntLiteral:    return "int";
        case ast::Kind::kCharLiteral:   return "char";
        case ast::Kind::kBoolLiteral:   return "bool";
        case ast::Kind::kFloatLiteral:  return "float";
        case ast::Kind::kStringLiteral: return "char[]";
        case ast::Kind::kIdentExpr: {
            auto it = syms.find(expr.name);
            if (it == syms.end()) return "";
            return it->second.slids_type;
        }
        case ast::Kind::kUnaryExpr: {
            std::string const& op = expr.text;
            if (op == "!") return "bool";
            if (op == "+" || op == "-" || op == "~") {
                if (expr.children.empty()) return "";
                return exprType(*expr.children[0], syms);
            }
            return "";
        }
        case ast::Kind::kBinaryExpr: {
            std::string const& op = expr.text;
            if (op == "==" || op == "!=" || op == "<" || op == "<="
             || op == ">"  || op == ">="
             || op == "&&" || op == "||" || op == "^^") return "bool";
            if (expr.children.empty()) return "";
            std::string lt = exprType(*expr.children[0], syms);
            if (!lt.empty()) return lt;
            if (expr.children.size() >= 2)
                return exprType(*expr.children[1], syms);
            return "";
        }
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kVarDeclStmt:
        case ast::Kind::kAssignStmt:
        case ast::Kind::kCallStmt:
        case ast::Kind::kReturnStmt:
            return "";
    }
    return "";
}

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag) {
    strings::Pool pool;

    std::ostringstream body;
    for (auto const& n : tree.nodes) {
        if (n->kind != ast::Kind::kProgram) continue;
        for (auto const& fn : n->children) {
            if (fn->kind == ast::Kind::kFunctionDef) {
                emitFunction(*fn, pool, body, diag);
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
