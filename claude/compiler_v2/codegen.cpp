#include "codegen.h"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
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

std::string llvmTypeFor(std::string const& slids_type,
                        int file_id, int tok,
                        diagnostic::Sink& diag) {
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
    diagnostic::report(diag, {file_id, tok,
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

std::string newLabel(char const* tag) {
    static int n = 0;
    return std::string(tag) + "_" + std::to_string(n++);
}

bool isLiteralKind(ast::Node const& n) {
    return n.kind == ast::Kind::kIntLiteral
        || n.kind == ast::Kind::kUintLiteral
        || n.kind == ast::Kind::kCharLiteral
        || n.kind == ast::Kind::kBoolLiteral
        || n.kind == ast::Kind::kFloatLiteral;
}

// Text to pass to widen::intLiteralFits / floatLiteralFits. Bool spelled
// "true"/"false" → integer "1"/"0"; everything else passes through.
std::string literalTextForFit(ast::Node const& n) {
    if (n.kind == ast::Kind::kBoolLiteral) return (n.text == "true") ? "1" : "0";
    return n.text;
}

// Default type a literal assumes when it can't flex into a partner type, per
// the widen.sl rules: int / int64 / uint64 by magnitude for ints; bool/char/
// float for the others.
std::string defaultLiteralType(ast::Node const& n) {
    switch (n.kind) {
        case ast::Kind::kIntLiteral: {
            std::string const& s = n.text;
            bool neg = !s.empty() && s[0] == '-';
            std::string mag_str = neg ? s.substr(1) : s;
            errno = 0;
            char* end = nullptr;
            uint64_t mag = std::strtoull(mag_str.c_str(), &end, 10);
            if (mag_str.empty() || end == mag_str.c_str() || *end != '\0'
                || errno == ERANGE) {
                return neg ? "int64" : "uint64";
            }
            if (neg) {
                if (mag <= (uint64_t)INT32_MAX + 1) return "int32";
                return "int64";
            }
            if (mag <= (uint64_t)INT32_MAX) return "int32";
            if (mag <= (uint64_t)INT64_MAX) return "int64";
            return "uint64";
        }
        case ast::Kind::kUintLiteral: {
            std::string const& s = n.text;
            errno = 0;
            char* end = nullptr;
            uint64_t mag = std::strtoull(s.c_str(), &end, 10);
            if (s.empty() || end == s.c_str() || *end != '\0' || errno == ERANGE) {
                return "uint64";
            }
            if (mag <= (uint64_t)UINT32_MAX) return "uint32";
            return "uint64";
        }
        case ast::Kind::kCharLiteral:  return "char";
        case ast::Kind::kBoolLiteral:  return "bool";
        case ast::Kind::kFloatLiteral: return "float32";
        case ast::Kind::kStringLiteral:
        case ast::Kind::kIdentExpr:
        case ast::Kind::kUnaryExpr:
        case ast::Kind::kBinaryExpr:
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kVarDeclStmt:
        case ast::Kind::kAssignStmt:
        case ast::Kind::kAugAssignStmt:
        case ast::Kind::kCallStmt:
        case ast::Kind::kReturnStmt:
            assert(false && "defaultLiteralType: not a literal kind");
            __builtin_unreachable();
    }
    assert(false && "defaultLiteralType: unhandled ast::Kind");
    __builtin_unreachable();
}

// Truthy coercion: 0-like values (false, 0, 0.0, null ptr) → i1 0; everything
// else → i1 1.
std::string emitToBool(std::string const& val, std::string const& slids_type,
                       std::ostream& out) {
    if (slids_type == "bool") return val;  // already i1
    if (isFloatType(slids_type)) {
        std::string llty = (slids_type == "float64") ? "double" : "float";
        std::string tmp = newTmp("tob");
        out << "  " << tmp << " = fcmp une " << llty << " "
            << val << ", 0.0\n";
        return tmp;
    }
    std::string llty;
    if      (slids_type == "char"   || slids_type == "int8"   || slids_type == "uint8")   llty = "i8";
    else if (slids_type == "int16"  || slids_type == "uint16")                            llty = "i16";
    else if (slids_type == "int"    || slids_type == "int32"
          || slids_type == "uint"   || slids_type == "uint32")                            llty = "i32";
    else if (slids_type == "int64"  || slids_type == "uint64" || slids_type == "intptr")  llty = "i64";
    else {
        assert(false && "emitToBool: unhandled slids type");
        __builtin_unreachable();
    }
    std::string tmp = newTmp("tob");
    out << "  " << tmp << " = icmp ne " << llty << " " << val << ", 0\n";
    return tmp;
}

// Short-circuit && / || / ^^ borrowed from v1.
std::string emitLogical(ast::Node const& expr, SymTab const& syms,
                        strings::Pool& pool, std::ostream& out,
                        diagnostic::Sink& diag) {
    assert(expr.children.size() == 2 && "emitLogical: BinaryExpr needs 2 children");
    std::string const& op = expr.text;
    ast::Node const& lhs = *expr.children[0];
    ast::Node const& rhs = *expr.children[1];

    std::string result_ptr = newTmp("sc");
    out << "  " << result_ptr << " = alloca i1\n";

    std::string lty = exprType(lhs, syms);
    if (lty.empty()) lty = "int";
    std::string lv = emitExpr(lhs, syms, pool, out, diag, lty);
    std::string left_bool = emitToBool(lv, lty, out);

    std::string eval_right = newLabel("sc_right");
    std::string done       = newLabel("sc_done");

    if (op == "&&") {
        out << "  store i1 0, ptr " << result_ptr << "\n";
        out << "  br i1 " << left_bool
            << ", label %" << eval_right
            << ", label %" << done << "\n";
    } else if (op == "||") {
        out << "  store i1 1, ptr " << result_ptr << "\n";
        out << "  br i1 " << left_bool
            << ", label %" << done
            << ", label %" << eval_right << "\n";
    } else if (op == "^^") {
        out << "  store i1 0, ptr " << result_ptr << "\n";
        out << "  br label %" << eval_right << "\n";
    } else {
        assert(false && "emitLogical: unhandled logical op");
        __builtin_unreachable();
    }

    out << eval_right << ":\n";
    std::string rty = exprType(rhs, syms);
    if (rty.empty()) rty = "int";
    std::string rv = emitExpr(rhs, syms, pool, out, diag, rty);
    std::string right_bool = emitToBool(rv, rty, out);

    std::string store_val = right_bool;
    if (op == "^^") {
        std::string xor_tmp = newTmp("xxor");
        out << "  " << xor_tmp << " = xor i1 "
            << left_bool << ", " << right_bool << "\n";
        store_val = xor_tmp;
    }
    out << "  store i1 " << store_val << ", ptr " << result_ptr << "\n";
    out << "  br label %" << done << "\n";
    out << done << ":\n";

    std::string result = newTmp("scv");
    out << "  " << result << " = load i1, ptr " << result_ptr << "\n";
    return result;
}

std::string emitUnary(ast::Node const& expr, SymTab const& syms,
                      strings::Pool& pool, std::ostream& out,
                      diagnostic::Sink& diag,
                      std::string const& dest_type) {
    assert(expr.children.size() == 1 && "emitUnary: UnaryExpr needs 1 child");
    std::string const& op = expr.text;
    ast::Node const& operand = *expr.children[0];

    if (op == "+") {
        return emitExpr(operand, syms, pool, out, diag, dest_type);
    }
    if (op == "-") {
        std::string v = emitExpr(operand, syms, pool, out, diag, dest_type);
        std::string llty = llvmTypeFor(dest_type, expr.file_id, expr.tok, diag);
        std::string tmp = newTmp("neg");
        if (isFloatType(dest_type)) {
            out << "  " << tmp << " = fneg " << llty << " " << v << "\n";
        } else {
            out << "  " << tmp << " = sub " << llty << " 0, " << v << "\n";
        }
        return tmp;
    }
    if (op == "~") {
        std::string v = emitExpr(operand, syms, pool, out, diag, dest_type);
        std::string llty = llvmTypeFor(dest_type, expr.file_id, expr.tok, diag);
        std::string tmp = newTmp("bnot");
        out << "  " << tmp << " = xor " << llty << " " << v << ", -1\n";
        return tmp;
    }
    if (op == "!") {
        std::string operand_type = exprType(operand, syms);
        if (operand_type.empty()) operand_type = "int";
        std::string v = emitExpr(operand, syms, pool, out, diag, operand_type);
        std::string llty = llvmTypeFor(operand_type, expr.file_id, expr.tok, diag);
        std::string tmp = newTmp("lnot");
        if (isFloatType(operand_type)) {
            out << "  " << tmp << " = fcmp oeq " << llty << " "
                << v << ", 0.0\n";
        } else {
            out << "  " << tmp << " = icmp eq " << llty << " "
                << v << ", 0\n";
        }
        return tmp;
    }
    diagnostic::report(diag, {expr.file_id, expr.tok,
        "codegen: unary operator '" + op + "' not yet supported", {}});
    return "0";
}

std::string emitBinary(ast::Node const& expr, SymTab const& syms,
                       strings::Pool& pool, std::ostream& out,
                       diagnostic::Sink& diag,
                       std::string const& dest_type) {
    assert(expr.children.size() == 2 && "emitBinary: BinaryExpr needs 2 children");
    std::string const& op = expr.text;
    ast::Node const& lhs = *expr.children[0];
    ast::Node const& rhs = *expr.children[1];

    if (op == "&&" || op == "||" || op == "^^") {
        std::string r = emitLogical(expr, syms, pool, out, diag);
        return widen::convert(r, "bool", dest_type,
                              expr.file_id, expr.tok, out, diag);
    }

    if (op == "<<" || op == ">>") {
        std::string lt = exprType(lhs, syms);
        if (lt.empty()) lt = "int32";

        std::string rt;
        if (isLiteralKind(rhs)) {
            // For float lhs, the int literal can't flex into the float type
            // (no silent mix), so it falls to its default; for int lhs,
            // attempt the usual literal-flex into lt.
            bool fits = false;
            if (!isFloatType(lt)) {
                fits = (rhs.kind == ast::Kind::kFloatLiteral)
                    ? widen::floatLiteralFits(rhs.text, lt)
                    : widen::intLiteralFits(literalTextForFit(rhs), lt);
            }
            rt = fits ? lt : defaultLiteralType(rhs);
        } else {
            rt = exprType(rhs, syms);
            if (rt.empty()) rt = "int32";
        }
        // Shift count must be integer-class per fold.sl:82.
        if (isFloatType(rt)) {
            diagnostic::report(diag, {expr.file_id, expr.tok,
                "Shift count must be integer-class; got '" + rt + "'.", {}});
            return "0";
        }

        std::string lv = emitExpr(lhs, syms, pool, out, diag, lt);
        std::string rv = emitExpr(rhs, syms, pool, out, diag, rt);

        if (isFloatType(lt)) {
            // Per fold.sl:128-131: `f << r` ≡ `f * (1<<r)`; `f >> r` ≡ `f / (1<<r)`.
            std::string rllty = llvmTypeFor(rt, expr.file_id, expr.tok, diag);
            std::string fllty = llvmTypeFor(lt, expr.file_id, expr.tok, diag);
            std::string pow2 = newTmp("pow2");
            out << "  " << pow2 << " = shl " << rllty << " 1, " << rv << "\n";
            std::string pow2f = newTmp("pow2f");
            out << "  " << pow2f << " = uitofp " << rllty << " " << pow2
                << " to " << fllty << "\n";
            std::string tmp = newTmp("bin");
            char const* instr = (op == "<<") ? "fmul" : "fdiv";
            out << "  " << tmp << " = " << instr << " " << fllty
                << " " << lv << ", " << pow2f << "\n";
            return widen::convert(tmp, lt, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }

        widen::TypeKind lk, rk;
        widen::classify(lt, lk);
        widen::classify(rt, rk);
        std::string lllty = llvmTypeFor(lt, expr.file_id, expr.tok, diag);
        std::string rllty = llvmTypeFor(rt, expr.file_id, expr.tok, diag);
        if (rk.bits != lk.bits) {
            std::string tmp = newTmp("shft");
            if (rk.bits > lk.bits) {
                out << "  " << tmp << " = trunc " << rllty << " " << rv
                    << " to " << lllty << "\n";
            } else /* rk.bits < lk.bits — zext to wider */ {
                out << "  " << tmp << " = zext " << rllty << " " << rv
                    << " to " << lllty << "\n";
            }
            rv = tmp;
        }
        bool uns = isUnsignedType(lt);
        std::string instr = (op == "<<") ? "shl" : (uns ? "lshr" : "ashr");
        std::string tmp = newTmp("bin");
        out << "  " << tmp << " = " << instr << " " << lllty
            << " " << lv << ", " << rv << "\n";
        return widen::convert(tmp, lt, dest_type,
                              expr.file_id, expr.tok, out, diag);
    }

    auto effectiveType = [&](ast::Node const& self, ast::Node const& other) -> std::string {
        if (!isLiteralKind(self)) {
            std::string t = exprType(self, syms);
            return t.empty() ? std::string("int32") : t;
        }
        if (!isLiteralKind(other)) {
            std::string other_ty = exprType(other, syms);
            if (!other_ty.empty()) {
                bool fits = (self.kind == ast::Kind::kFloatLiteral)
                    ? widen::floatLiteralFits(self.text, other_ty)
                    : widen::intLiteralFits(literalTextForFit(self), other_ty);
                if (fits) return other_ty;
            }
        }
        return defaultLiteralType(self);
    };

    std::string lt = effectiveType(lhs, rhs);
    std::string rt = effectiveType(rhs, lhs);

    std::string opty;
    if (!widen::commonType(lt, rt, opty)) {
        diagnostic::report(diag, {expr.file_id, expr.tok,
            "No common type for '" + lt + "' and '" + rt
            + "'; use an explicit type conversion.", {}});
        return "0";
    }

    std::string lv = emitExpr(lhs, syms, pool, out, diag, opty);
    std::string rv = emitExpr(rhs, syms, pool, out, diag, opty);

    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        std::string llty = llvmTypeFor(opty, expr.file_id, expr.tok, diag);
        bool flt = isFloatType(opty);
        bool uns = isUnsignedType(opty) || opty == "bool";
        char const* pred;
        if      (op == "==") pred = flt ? "oeq" : "eq";
        else if (op == "!=") pred = flt ? "one" : "ne";
        else if (op == "<")  pred = flt ? "olt" : (uns ? "ult" : "slt");
        else if (op == "<=") pred = flt ? "ole" : (uns ? "ule" : "sle");
        else if (op == ">")  pred = flt ? "ogt" : (uns ? "ugt" : "sgt");
        else if (op == ">=") pred = flt ? "oge" : (uns ? "uge" : "sge");
        else {
            // Reachable only if the outer comparison-op guard grows a new op
            // without extending this chain.
            assert(false && "emitBinary cmp: unhandled comparison op");
            __builtin_unreachable();
        }
        std::string tmp = newTmp("cmp");
        out << "  " << tmp << " = " << (flt ? "fcmp " : "icmp ")
            << pred << " " << llty << " " << lv << ", " << rv << "\n";
        return widen::convert(tmp, "bool", dest_type,
                              expr.file_id, expr.tok, out, diag);
    }

    std::string llty = llvmTypeFor(opty, expr.file_id, expr.tok, diag);
    bool flt = isFloatType(opty);
    bool uns = isUnsignedType(opty);

    std::string instr;
    if (flt) {
        if (op == "+") instr = "fadd";
        else if (op == "-") instr = "fsub";
        else if (op == "*") instr = "fmul";
        else if (op == "/") instr = "fdiv";
        else if (op == "%") instr = "frem";
    } else {
        if      (op == "+") instr = "add";
        else if (op == "-") instr = "sub";
        else if (op == "*") instr = "mul";
        else if (op == "/") instr = uns ? "udiv" : "sdiv";
        else if (op == "%") instr = uns ? "urem" : "srem";
        else if (op == "&") instr = "and";
        else if (op == "|") instr = "or";
        else if (op == "^") instr = "xor";
    }
    if (instr.empty()) {
        // Today this fires only for float + bitwise (& | ^) — every int op is
        // mapped above, and every float arith op is mapped after `%` landed.
        if (flt) {
            diagnostic::report(diag, {expr.file_id, expr.tok,
                std::string("Bitwise '") + op + "' not defined on floating-point type '"
                + opty + "'.", {}});
        } else {
            // Defensive: future ops (slid-typed, pointer-typed) would land here.
            diagnostic::report(diag, {expr.file_id, expr.tok,
                "codegen: binary operator '" + op + "' on '" + opty
                + "' not yet supported", {}});
        }
        return "0";
    }
    std::string tmp = newTmp("bin");
    out << "  " << tmp << " = " << instr << " " << llty
        << " " << lv << ", " << rv << "\n";
    return widen::convert(tmp, opty, dest_type,
                          expr.file_id, expr.tok, out, diag);
}

}  // namespace

std::string emitExpr(ast::Node const& expr, SymTab const& syms,
                     strings::Pool& pool, std::ostream& out,
                     diagnostic::Sink& diag,
                     std::string const& dest_type) {
    switch (expr.kind) {
        case ast::Kind::kIntLiteral:
        case ast::Kind::kUintLiteral:
        case ast::Kind::kCharLiteral:
        case ast::Kind::kBoolLiteral: {
            widen::checkIntLiteralFits(expr.text, dest_type,
                                       expr.file_id, expr.tok, diag);
            return expr.text;
        }
        case ast::Kind::kFloatLiteral: {
            widen::checkFloatLiteralFits(expr.text, dest_type,
                                         expr.file_id, expr.tok, diag);
            return normalizeFloatLiteral(expr.text);
        }
        case ast::Kind::kStringLiteral: {
            int id = strings::add(pool, expr.text);
            return std::string("@.str_") + std::to_string(id);
        }
        case ast::Kind::kIdentExpr: {
            auto it = syms.find(expr.name);
            if (it == syms.end()) {
                diagnostic::report(diag, {expr.file_id, expr.tok,
                    "codegen: unknown variable '" + expr.name + "'", {}});
                return "0";
            }
            std::string tmp = newTmp("ld");
            out << "  " << tmp << " = load " << it->second.llvm_type
                << ", ptr " << it->second.alloca_name << "\n";
            return widen::convert(tmp, it->second.slids_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
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
        case ast::Kind::kAugAssignStmt:
        case ast::Kind::kCallStmt:
        case ast::Kind::kReturnStmt:
            diagnostic::report(diag, {expr.file_id, expr.tok,
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
            std::string llty = llvmTypeFor(stmt.return_type,
                                           stmt.file_id, stmt.tok, diag);
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
                diagnostic::report(diag, {stmt.file_id, stmt.tok,
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
                diagnostic::report(diag, {stmt.file_id, stmt.tok,
                    "codegen: unknown call '" + stmt.name + "'", {}});
            }
            return;
        case ast::Kind::kReturnStmt: {
            std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                       fn_return_type);
            std::string llty = llvmTypeFor(fn_return_type,
                                           stmt.file_id, stmt.tok, diag);
            out << "  ret " << llty << " " << val << "\n";
            return;
        }
        case ast::Kind::kAugAssignStmt:
            diagnostic::report(diag, {stmt.file_id, stmt.tok,
                "codegen: kAugAssignStmt survived desugar", {}});
            return;
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kStringLiteral:
        case ast::Kind::kIntLiteral:
        case ast::Kind::kUintLiteral:
        case ast::Kind::kCharLiteral:
        case ast::Kind::kBoolLiteral:
        case ast::Kind::kFloatLiteral:
        case ast::Kind::kIdentExpr:
        case ast::Kind::kUnaryExpr:
        case ast::Kind::kBinaryExpr:
            diagnostic::report(diag, {stmt.file_id, stmt.tok,
                "codegen: unexpected node kind in statement position", {}});
            return;
    }
    assert(false && "emitStmt: unhandled ast::Kind");
    __builtin_unreachable();
}

void emitFunction(ast::Node const& fn, strings::Pool& pool,
                  std::ostream& out, diagnostic::Sink& diag) {
    std::string ret_llty = llvmTypeFor(fn.return_type,
                                       fn.file_id, fn.tok, diag);
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
        case ast::Kind::kUintLiteral:   return "uint";
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
            assert(expr.children.size() == 1
                && "exprType: UnaryExpr needs 1 child");
            std::string const& op = expr.text;
            if (op == "!") return "bool";
            if (op == "+" || op == "-" || op == "~") {
                return exprType(*expr.children[0], syms);
            }
            assert(false && "exprType: unhandled unary op");
            __builtin_unreachable();
        }
        case ast::Kind::kBinaryExpr: {
            assert(expr.children.size() == 2
                && "exprType: BinaryExpr needs 2 children");
            std::string const& op = expr.text;
            if (op == "==" || op == "!=" || op == "<" || op == "<="
             || op == ">"  || op == ">="
             || op == "&&" || op == "||" || op == "^^") return "bool";
            std::string lt = exprType(*expr.children[0], syms);
            if (!lt.empty()) return lt;
            return exprType(*expr.children[1], syms);
        }
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kVarDeclStmt:
        case ast::Kind::kAssignStmt:
        case ast::Kind::kAugAssignStmt:
        case ast::Kind::kCallStmt:
        case ast::Kind::kReturnStmt:
            assert(false && "exprType: called on a statement-kind node");
            __builtin_unreachable();
    }
    assert(false && "exprType: unhandled ast::Kind");
    __builtin_unreachable();
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
                diagnostic::report(diag, {fn->file_id, fn->tok,
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
