#include "codegen.h"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    (void)file_id; (void)tok; (void)diag;
    assert(false && "llvmTypeFor: classify let through an unknown type");
    __builtin_unreachable();
}

std::string normalizeFloatLiteral(std::string const& text) {
    if (text.find('.') != std::string::npos) return text;
    auto e = text.find_first_of("eE");
    std::string out = text;
    if (e == std::string::npos) out += ".0";
    else out.insert(e, ".0");
    return out;
}

// LLVM textual float32 literals must be exact at float32 precision. Decimal
// text only works for values whose decimal form parses back to the same
// float32 bit pattern (e.g. "4.0", "0.5"). Lossy values (3.14, 0.1, etc.)
// require the hex bit-pattern form: 0x followed by 16 hex digits encoding
// the float32-rounded value padded to double precision. Computation per
// fold.sl:66-67 / todo.txt item 7:
//   source decimal -> double -> (float) cast -> re-promote to double
//   -> uint64 bit pattern -> "0x" + 16 hex digits.
std::string float32HexLiteral(std::string const& text) {
    errno = 0;
    char* end = nullptr;
    double d = std::strtod(text.c_str(), &end);
    assert(end != text.c_str() && *end == '\0' && errno != ERANGE
        && "float32HexLiteral: malformed text from numeric");
    float f = static_cast<float>(d);
    double back = static_cast<double>(f);
    static_assert(sizeof(uint64_t) == sizeof(double), "double must be 64 bits");
    uint64_t bits;
    std::memcpy(&bits, &back, sizeof(bits));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llX",
                  static_cast<unsigned long long>(bits));
    return buf;
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
    if (slids_type.size() >= 2 && slids_type.substr(slids_type.size() - 2) == "[]") {
        std::string tmp = newTmp("tob");
        out << "  " << tmp << " = icmp ne ptr " << val << ", null\n";
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

    std::string const& lty = lhs.inferred_type;
    assert(!lty.empty() && "emitLogical: lhs missing inferred_type");
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
    std::string const& rty = rhs.inferred_type;
    assert(!rty.empty() && "emitLogical: rhs missing inferred_type");
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
        std::string const& operand_type = operand.inferred_type;
        assert(!operand_type.empty() && "emitUnary '!': operand missing inferred_type");
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
    (void)diag;
    assert(false && "emitUnary: grammar produced an unknown unary op");
    __builtin_unreachable();
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
        std::string const& lt = lhs.inferred_type;
        std::string const& rt = rhs.inferred_type;
        assert(!lt.empty() && "emitBinary shift: lhs missing inferred_type");
        assert(!rt.empty() && "emitBinary shift: rhs missing inferred_type");
        // Classify already rejected non-integer rhs; nothing to recheck here.

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

    std::string const& opty = expr.op_type;
    assert(!opty.empty() && "emitBinary: BinaryExpr missing op_type");

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
    assert(!instr.empty()
        && "emitBinary: no instruction mapped — classify should have rejected "
           "(float bitwise) or covered (all int ops, all float arith)");
    std::string tmp = newTmp("bin");
    out << "  " << tmp << " = " << instr << " " << llty
        << " " << lv << ", " << rv << "\n";
    return widen::convert(tmp, opty, dest_type,
                          expr.file_id, expr.tok, out, diag);
}

// Emit a user-function call (statement or expression form). classify stamped
// return_type + param_types on the node. Returns the result register, or ""
// for a void return. The caller decides whether to use, widen, or drop it.
std::string emitCall(ast::Node const& call, SymTab const& syms,
                     strings::Pool& pool, std::ostream& out,
                     diagnostic::Sink& diag) {
    assert(call.children.size() == call.param_types.size()
        && "emitCall: arity should have been verified by classify");
    std::vector<std::pair<std::string, std::string>> arg_vals;
    arg_vals.reserve(call.children.size());
    for (size_t i = 0; i < call.children.size(); i++) {
        ast::Node const& arg = *call.children[i];
        std::string const& dest = call.param_types[i];
        std::string val = emitExpr(arg, syms, pool, out, diag, dest);
        std::string llty = llvmTypeFor(dest, arg.file_id, arg.tok, diag);
        arg_vals.push_back({llty, std::move(val)});
    }
    std::string ret_llty = llvmTypeFor(call.return_type,
                                       call.file_id, call.tok, diag);
    std::string result;
    out << "  ";
    if (call.return_type != "void") {
        result = newTmp("call");
        out << result << " = ";
    }
    out << "call " << ret_llty << " @" << call.name << "(";
    for (size_t i = 0; i < arg_vals.size(); i++) {
        if (i > 0) out << ", ";
        out << arg_vals[i].first << " " << arg_vals[i].second;
    }
    out << ")\n";
    return result;
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
            if (dest_type == "float" || dest_type == "float32") {
                return float32HexLiteral(expr.text);
            }
            return normalizeFloatLiteral(expr.text);
        }
        case ast::Kind::kStringLiteral: {
            int id = strings::add(pool, expr.text);
            return std::string("@.str_") + std::to_string(id);
        }
        case ast::Kind::kIdentExpr: {
            assert(expr.resolved_entry_id >= 0
                && "emitExpr kIdentExpr: classify did not stamp resolved_entry_id");
            auto it = syms.find(expr.resolved_entry_id);
            assert(it != syms.end()
                && "emitExpr kIdentExpr: entry not in SymTab (alloca never emitted?)");
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
        case ast::Kind::kCallExpr: {
            std::string r = emitCall(expr, syms, pool, out, diag);
            return widen::convert(r, expr.return_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kVarDeclStmt:
        case ast::Kind::kAssignStmt:
        case ast::Kind::kAugAssignStmt:
        case ast::Kind::kCallStmt:
        case ast::Kind::kReturnStmt:
        case ast::Kind::kParam:
            assert(false && "emitExpr: reached statement-kind node");
            __builtin_unreachable();
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
            // Consts are substituted away by constfold and have no runtime
            // representation. Skip the alloca + store entirely.
            if (stmt.is_const) return;
            assert(stmt.resolved_entry_id >= 0
                && "kVarDeclStmt: classify did not stamp resolved_entry_id");
            std::string llty = llvmTypeFor(stmt.return_type,
                                           stmt.file_id, stmt.tok, diag);
            std::string regname = std::string("%") + stmt.name;
            out << "  " << regname << " = alloca " << llty << "\n";
            syms[stmt.resolved_entry_id] = {regname, llty, stmt.return_type};
            if (!stmt.children.empty()) {
                std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                           stmt.return_type);
                out << "  store " << llty << " " << val
                    << ", ptr " << regname << "\n";
            }
            return;
        }
        case ast::Kind::kAssignStmt: {
            assert(stmt.resolved_entry_id >= 0
                && "kAssignStmt: classify did not stamp resolved_entry_id");
            auto it = syms.find(stmt.resolved_entry_id);
            assert(it != syms.end()
                && "kAssignStmt: entry not in SymTab (alloca never emitted?)");
            std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                       it->second.slids_type);
            out << "  store " << it->second.llvm_type << " " << val
                << ", ptr " << it->second.alloca_name << "\n";
            return;
        }
        case ast::Kind::kCallStmt: {
            if (print::tryEmitCall(stmt, syms, pool, out, diag)) return;
            // Statement form discards the result register.
            emitCall(stmt, syms, pool, out, diag);
            return;
        }
        case ast::Kind::kReturnStmt: {
            std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                       fn_return_type);
            std::string llty = llvmTypeFor(fn_return_type,
                                           stmt.file_id, stmt.tok, diag);
            out << "  ret " << llty << " " << val << "\n";
            return;
        }
        case ast::Kind::kAugAssignStmt:
            assert(false && "emitStmt: AugAssign survived desugar");
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
        case ast::Kind::kCallExpr:
        case ast::Kind::kParam:
            assert(false && "emitStmt: reached non-statement node kind");
            return;
    }
    assert(false && "emitStmt: unhandled ast::Kind");
    __builtin_unreachable();
}

void emitFunction(ast::Node const& fn, strings::Pool& pool,
                  std::ostream& out, diagnostic::Sink& diag) {
    std::string ret_llty = llvmTypeFor(fn.return_type,
                                       fn.file_id, fn.tok, diag);
    out << "define " << ret_llty << " @" << fn.name << "(";
    for (size_t i = 0; i < fn.params.size(); i++) {
        ast::Node const& p = *fn.params[i];
        if (i > 0) out << ", ";
        std::string p_llty = llvmTypeFor(p.return_type, p.file_id, p.tok, diag);
        out << p_llty << " %arg." << i;
    }
    out << ") {\n";

    SymTab syms;
    // Alloca + store-in each param so the body can read/write it like a local.
    // Register under the param's resolved_entry_id (stamped by classify's
    // body-frame seeding).
    for (size_t i = 0; i < fn.params.size(); i++) {
        ast::Node const& p = *fn.params[i];
        std::string p_llty = llvmTypeFor(p.return_type, p.file_id, p.tok, diag);
        std::string regname = std::string("%") + p.name;
        out << "  " << regname << " = alloca " << p_llty << "\n";
        out << "  store " << p_llty << " %arg." << i
            << ", ptr " << regname << "\n";
        syms[p.resolved_entry_id] = {regname, p_llty, p.return_type};
    }
    for (auto const& s : fn.children) {
        emitStmt(*s, syms, pool, fn.return_type, out, diag);
    }
    out << "}\n";
}

}  // namespace

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
            } else if (fn->kind == ast::Kind::kVarDeclStmt && fn->is_const) {
                // intentional n/a: file-scope const has no runtime form;
                // constfold substituted every use to a literal.
            } else {
                assert(false
                    && "codegen run: unexpected node kind at program scope "
                       "(resolve should have rejected)");
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
