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

// Logical && / || (short-circuit) and ^^ (full) via phi nodes — NOT alloca. An
// alloca here would land in the current block, and when the logical is a loop
// CONDITION that block is the loop header: a fresh stack slot would leak on every
// iteration (the same class as the loop-body-alloca stack-overflow bug). The phi
// keeps the value in SSA. For && / ||, the short-circuit edge and the
// rhs-completion edge are each routed through a dedicated single-predecessor
// block so the phi's predecessor labels are always known — even when rhs itself
// emits nested blocks (a nested logical), in which case right_bool is defined in
// rhs's last block, which dominates the routing block and so the merge edge.
std::string emitLogical(ast::Node const& expr, SymTab const& syms,
                        strings::Pool& pool, std::ostream& out,
                        diagnostic::Sink& diag) {
    assert(expr.children.size() == 2 && "emitLogical: BinaryExpr needs 2 children");
    std::string const& op = expr.text;
    ast::Node const& lhs = *expr.children[0];
    ast::Node const& rhs = *expr.children[1];

    std::string const& lty = lhs.inferred_type;
    assert(!lty.empty() && "emitLogical: lhs missing inferred_type");
    std::string lv = emitExpr(lhs, syms, pool, out, diag, lty);
    std::string left_bool = emitToBool(lv, lty, out);

    std::string const& rty = rhs.inferred_type;
    assert(!rty.empty() && "emitLogical: rhs missing inferred_type");

    if (op == "^^") {
        // Logical xor cannot short-circuit (it needs both operands): evaluate
        // both, xor. Straight-line — no branches, no phi.
        std::string rv = emitExpr(rhs, syms, pool, out, diag, rty);
        std::string right_bool = emitToBool(rv, rty, out);
        std::string res = newTmp("xxor");
        out << "  " << res << " = xor i1 " << left_bool << ", "
            << right_bool << "\n";
        return res;
    }

    assert((op == "&&" || op == "||") && "emitLogical: unhandled logical op");
    // && short-circuits to false when left is false; || to true when left true.
    std::string rhs_lbl   = newLabel("sc_rhs");
    std::string short_lbl = newLabel("sc_short");
    std::string rhs_end   = newLabel("sc_rhs_end");
    std::string done_lbl  = newLabel("sc_done");
    char const* short_val = (op == "&&") ? "false" : "true";

    if (op == "&&") {
        out << "  br i1 " << left_bool << ", label %" << rhs_lbl
            << ", label %" << short_lbl << "\n";
    } else {
        out << "  br i1 " << left_bool << ", label %" << short_lbl
            << ", label %" << rhs_lbl << "\n";
    }

    // Short-circuit edge: a known-label block carrying the constant result.
    out << short_lbl << ":\n";
    out << "  br label %" << done_lbl << "\n";

    // RHS edge: evaluate rhs, then route through a dedicated single-predecessor
    // block so the phi predecessor is a known label even if rhs emitted blocks.
    out << rhs_lbl << ":\n";
    std::string rv = emitExpr(rhs, syms, pool, out, diag, rty);
    std::string right_bool = emitToBool(rv, rty, out);
    out << "  br label %" << rhs_end << "\n";
    out << rhs_end << ":\n";
    out << "  br label %" << done_lbl << "\n";

    out << done_lbl << ":\n";
    std::string res = newTmp("scv");
    out << "  " << res << " = phi i1 [ " << short_val << ", %" << short_lbl
        << " ], [ " << right_bool << ", %" << rhs_end << " ]\n";
    return res;
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
    // A nested-function call passes each capture by reference: the host
    // variable's alloca address (already a ptr in the host's SymTab).
    for (int cid : call.captures) {
        auto it = syms.find(cid);
        if (it != syms.end()) {
            arg_vals.push_back({"ptr", it->second.alloca_name});
        }
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
            assert(expr.return_type != "void"
                && "emitExpr kCallExpr: classify should have rejected void call-as-value");
            std::string r = emitCall(expr, syms, pool, out, diag);
            return widen::convert(r, expr.return_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        case ast::Kind::kSeqExpr: {
            // Children emit in order. The value_index child is the result
            // (widened into dest_type); the rest are bumps run for effect.
            assert(expr.value_index >= 0
                && expr.value_index < static_cast<int>(expr.children.size())
                && "kSeqExpr: value_index out of range");
            std::string result;
            for (size_t i = 0; i < expr.children.size(); i++) {
                if (static_cast<int>(i) == expr.value_index) {
                    result = emitExpr(*expr.children[i], syms, pool, out, diag, dest_type);
                } else {
                    emitExpr(*expr.children[i], syms, pool, out, diag, "");
                }
            }
            return result;
        }
        case ast::Kind::kBumpExpr: {
            // `x = x ± 1` on a scalar variable; the returned register (the new
            // value) is discarded by the enclosing seq.
            assert(expr.resolved_entry_id >= 0 && "kBumpExpr: missing entry");
            auto it = syms.find(expr.resolved_entry_id);
            assert(it != syms.end() && "kBumpExpr: entry not in SymTab");
            std::string const& llty = it->second.llvm_type;
            bool flt = isFloatType(it->second.slids_type);
            std::string cur = newTmp("ld");
            out << "  " << cur << " = load " << llty << ", ptr "
                << it->second.alloca_name << "\n";
            std::string nv = newTmp("inc");
            char const* instr = flt ? (expr.text == "++" ? "fadd" : "fsub")
                                    : (expr.text == "++" ? "add" : "sub");
            char const* one = flt ? "1.0" : "1";
            out << "  " << nv << " = " << instr << " " << llty << " "
                << cur << ", " << one << "\n";
            out << "  store " << llty << " " << nv << ", ptr "
                << it->second.alloca_name << "\n";
            return nv;
        }
        case ast::Kind::kPreIncExpr:
        case ast::Kind::kPostIncExpr:
            assert(false && "emitExpr: inc/dec survived desugar's PPID pass");
            __builtin_unreachable();
        case ast::Kind::kProgram:
        case ast::Kind::kFunctionDef:
        case ast::Kind::kFunctionDecl:
        case ast::Kind::kVarDeclStmt:
        case ast::Kind::kAssignStmt:
        case ast::Kind::kAugAssignStmt:
        case ast::Kind::kCallStmt:
        case ast::Kind::kExprStmt:
        case ast::Kind::kReturnStmt:
        case ast::Kind::kBlockStmt:
        case ast::Kind::kIfStmt:
        case ast::Kind::kWhileStmt:
        case ast::Kind::kDoWhileStmt:
        case ast::Kind::kForLongStmt:
        case ast::Kind::kBreakStmt:
        case ast::Kind::kContinueStmt:
        case ast::Kind::kSwitchStmt:
        case ast::Kind::kCaseClause:
        case ast::Kind::kParam:
            assert(false && "emitExpr: reached statement-kind node");
            __builtin_unreachable();
    }
    assert(false && "emitExpr: unhandled ast::Kind");
    __builtin_unreachable();
}

namespace {

bool endsInReturn(std::vector<std::unique_ptr<ast::Node>> const& stmts);
bool endsInReturnNode(ast::Node const& s);
bool endsTerminated(std::vector<std::unique_ptr<ast::Node>> const& stmts);
bool endsTerminatedNode(ast::Node const& s);
bool containsBreak(ast::Node const& s);

// The nearest enclosing loop's branch targets, threaded through emitStmt so a
// break/continue (possibly nested inside if-arms/blocks) reaches the right
// blocks. nullptr outside any loop (resolve already rejected break/continue
// there). continue -> header (re-test), break -> exit (after the loop).
struct LoopCtx {
    std::string header_label;
    std::string exit_label;
    LoopCtx const* outer = nullptr;   // enclosing loop/switch — a labeled/numbered
                                      // break/continue walks `loop_levels` hops out
};

void emitStmt(ast::Node const& stmt, SymTab& syms,
              strings::Pool& pool,
              std::string const& fn_return_type,
              LoopCtx const* loop,
              std::ostream& out, diagnostic::Sink& diag) {
    switch (stmt.kind) {
        case ast::Kind::kVarDeclStmt: {
            // Consts are substituted away by constfold and have no runtime form.
            if (stmt.is_const) return;
            assert(stmt.resolved_entry_id >= 0
                && "kVarDeclStmt: classify did not stamp resolved_entry_id");
            // The alloca + SymTab registration were HOISTED to the function entry
            // block by emitFunction (an alloca emitted here would re-allocate
            // stack on every pass through an enclosing loop — the v1 stack-
            // overflow bug). Here we only emit the initializer store, if any.
            auto it = syms.find(stmt.resolved_entry_id);
            assert(it != syms.end()
                && "kVarDeclStmt: alloca not hoisted to entry block");
            if (!stmt.children.empty()) {
                std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                           it->second.slids_type);
                out << "  store " << it->second.llvm_type << " " << val
                    << ", ptr " << it->second.alloca_name << "\n";
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
        case ast::Kind::kExprStmt: {
            // Evaluate the expression for its side effects; discard the value.
            emitExpr(*stmt.children[0], syms, pool, out, diag, "");
            return;
        }
        case ast::Kind::kReturnStmt: {
            // Bare `return;` (no child) in a void function -> `ret void`.
            if (stmt.children.empty()) {
                out << "  ret void\n";
                return;
            }
            std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                       fn_return_type);
            std::string llty = llvmTypeFor(fn_return_type,
                                           stmt.file_id, stmt.tok, diag);
            out << "  ret " << llty << " " << val << "\n";
            return;
        }
        case ast::Kind::kBlockStmt: {
            // A nested scope is transparent at codegen: emit its statements in
            // order (allocas were hoisted to the entry block; the SymTab is
            // entry-id-keyed, so a nested-scope local is reachable while live
            // without any scope bookkeeping here). Thread the loop context so a
            // break/continue nested in this block reaches the enclosing loop.
            for (auto const& ch : stmt.children) {
                if (ch) emitStmt(*ch, syms, pool, fn_return_type, loop, out, diag);
            }
            return;
        }
        case ast::Kind::kIfStmt: {
            // children[0] = condition, [1] = then-branch, [2] = optional else.
            // Evaluate the condition, truthy-coerce it, conditional-br to the
            // then/else (or merge) blocks, emit each arm, br to the merge. No phi:
            // definite-assignment is tracked via allocas upstream. An arm that
            // ends in a control transfer (return / break / continue) is already
            // terminated, so it must NOT emit the br-to-merge (two terminators is
            // invalid IR). If every arm transfers, the merge is unreachable AND
            // unterminated, so it is not emitted at all — resolve's 2A guarantees
            // no live statement follows such an if.
            assert(stmt.children.size() >= 2 && "kIfStmt needs condition + then");
            ast::Node const& cond = *stmt.children[0];
            std::string cv = emitExpr(cond, syms, pool, out, diag,
                                      cond.inferred_type);
            std::string cbool = emitToBool(cv, cond.inferred_type, out);

            bool has_else = stmt.children.size() > 2 && stmt.children[2];
            bool then_falls = !endsTerminated(stmt.children[1]->children);
            // A missing else is an implicit fall-through path to the merge.
            bool else_falls = !has_else || !endsTerminatedNode(*stmt.children[2]);
            bool merge_used = then_falls || else_falls;

            std::string then_lbl = newLabel("if_then");
            std::string else_lbl = has_else ? newLabel("if_else") : std::string();
            std::string merge_lbl = newLabel("if_end");

            out << "  br i1 " << cbool << ", label %" << then_lbl
                << ", label %" << (has_else ? else_lbl : merge_lbl) << "\n";

            out << then_lbl << ":\n";
            emitStmt(*stmt.children[1], syms, pool, fn_return_type, loop, out, diag);
            if (then_falls) out << "  br label %" << merge_lbl << "\n";

            if (has_else) {
                out << else_lbl << ":\n";
                emitStmt(*stmt.children[2], syms, pool, fn_return_type, loop, out, diag);
                if (else_falls) out << "  br label %" << merge_lbl << "\n";
            }

            if (merge_used) out << merge_lbl << ":\n";
            return;
        }
        case ast::Kind::kWhileStmt: {
            // children[0] = condition, [1] = body block. header tests the
            // condition and branches to body or exit; the body branches back to
            // header (continue target), break branches to exit. The back-edge is
            // emitted only if the body can fall through (doesn't end in a control
            // transfer), else it would double-terminate the body's last block.
            assert(stmt.children.size() == 2 && "kWhileStmt needs condition + body");
            std::string head_lbl = newLabel("while_head");
            std::string body_lbl = newLabel("while_body");
            std::string exit_lbl = newLabel("while_exit");

            out << "  br label %" << head_lbl << "\n";
            out << head_lbl << ":\n";
            ast::Node const& cond = *stmt.children[0];
            std::string cv = emitExpr(cond, syms, pool, out, diag,
                                      cond.inferred_type);
            std::string cbool = emitToBool(cv, cond.inferred_type, out);
            out << "  br i1 " << cbool << ", label %" << body_lbl
                << ", label %" << exit_lbl << "\n";

            out << body_lbl << ":\n";
            LoopCtx ctx{head_lbl, exit_lbl, loop};
            emitStmt(*stmt.children[1], syms, pool, fn_return_type, &ctx, out, diag);
            if (!endsTerminated(stmt.children[1]->children)) {
                out << "  br label %" << head_lbl << "\n";   // back-edge
            }

            out << exit_lbl << ":\n";
            return;
        }
        case ast::Kind::kDoWhileStmt: {
            // children[0] = condition, [1] = body. Body-first: enter the body
            // unconditionally, then test the condition. continue jumps to the
            // test (cond block), break to exit. The body's back-edge (to cond) is
            // emitted only if the body can fall through.
            assert(stmt.children.size() == 2 && "kDoWhileStmt needs condition + body");
            std::string body_lbl = newLabel("do_body");
            std::string cond_lbl = newLabel("do_cond");
            std::string exit_lbl = newLabel("do_exit");

            out << "  br label %" << body_lbl << "\n";
            out << body_lbl << ":\n";
            LoopCtx ctx{cond_lbl, exit_lbl, loop};   // continue -> cond, break -> exit
            emitStmt(*stmt.children[1], syms, pool, fn_return_type, &ctx, out, diag);
            if (!endsTerminated(stmt.children[1]->children)) {
                out << "  br label %" << cond_lbl << "\n";
            }

            out << cond_lbl << ":\n";
            ast::Node const& cond = *stmt.children[0];
            std::string cv = emitExpr(cond, syms, pool, out, diag,
                                      cond.inferred_type);
            std::string cbool = emitToBool(cv, cond.inferred_type, out);
            out << "  br i1 " << cbool << ", label %" << body_lbl
                << ", label %" << exit_lbl << "\n";

            out << exit_lbl << ":\n";
            return;
        }
        case ast::Kind::kForLongStmt: {
            // children[0]=cond, [1]=update, [2]=body, [3..]=varlist decls.
            // Run the varlist init stores once (allocas hoisted), then:
            // head: test cond -> body or exit; body -> update; update -> head.
            // continue -> update (then re-test), break -> exit.
            assert(stmt.children.size() >= 3 && "kForLongStmt needs cond+update+body");
            for (std::size_t i = 3; i < stmt.children.size(); i++) {
                if (stmt.children[i]) {
                    emitStmt(*stmt.children[i], syms, pool, fn_return_type, loop,
                             out, diag);
                }
            }
            std::string head_lbl = newLabel("for_head");
            std::string body_lbl = newLabel("for_body");
            std::string upd_lbl  = newLabel("for_update");
            std::string exit_lbl = newLabel("for_exit");

            out << "  br label %" << head_lbl << "\n";
            out << head_lbl << ":\n";
            ast::Node const& cond = *stmt.children[0];
            std::string cv = emitExpr(cond, syms, pool, out, diag,
                                      cond.inferred_type);
            std::string cbool = emitToBool(cv, cond.inferred_type, out);
            out << "  br i1 " << cbool << ", label %" << body_lbl
                << ", label %" << exit_lbl << "\n";

            out << body_lbl << ":\n";
            LoopCtx ctx{upd_lbl, exit_lbl, loop};   // continue -> update, break -> exit
            emitStmt(*stmt.children[2], syms, pool, fn_return_type, &ctx, out, diag);
            if (!endsTerminated(stmt.children[2]->children)) {
                out << "  br label %" << upd_lbl << "\n";
            }

            out << upd_lbl << ":\n";
            // The update can't break/continue/return (resolve-enforced), so it
            // always falls through back to the head. Pass the enclosing loop ctx
            // (a nested loop inside the update carries its own).
            emitStmt(*stmt.children[1], syms, pool, fn_return_type, loop, out, diag);
            out << "  br label %" << head_lbl << "\n";

            out << exit_lbl << ":\n";
            return;
        }
        case ast::Kind::kBreakStmt: {
            assert(loop && "kBreakStmt: break outside a loop survived resolve");
            // resolve stamped loop_levels = hops outward to the target frame.
            LoopCtx const* t = loop;
            for (int i = 0; i < stmt.loop_levels; i++) {
                assert(t->outer && "loop_levels exceeds the loop/switch context depth");
                t = t->outer;
            }
            out << "  br label %" << t->exit_label << "\n";
            return;
        }
        case ast::Kind::kContinueStmt: {
            assert(loop && "kContinueStmt: continue outside a loop survived resolve");
            LoopCtx const* t = loop;
            for (int i = 0; i < stmt.loop_levels; i++) {
                assert(t->outer && "loop_levels exceeds the loop/switch context depth");
                t = t->outer;
            }
            out << "  br label %" << t->header_label << "\n";
            return;
        }
        case ast::Kind::kSwitchStmt: {
            // children[0]=scrutinee, [1..]=kCaseClause. Lower to an `llvm switch`
            // dispatching to one block per clause (source order) + an exit; blocks
            // fall through via `br` to the next unless terminated. naked break ->
            // exit; continue passes through to the enclosing loop (swctx inherits
            // its header). default's block is the switch instr's default label
            // (exit when there is no default).
            assert(!stmt.children.empty() && "kSwitchStmt needs a scrutinee");
            ast::Node const& scrut = *stmt.children[0];
            std::string sv = emitExpr(scrut, syms, pool, out, diag,
                                      scrut.inferred_type);
            std::string llty = llvmTypeFor(scrut.inferred_type, scrut.file_id,
                                           scrut.tok, diag);
            std::size_t n = stmt.children.size() - 1;   // clause count
            std::string exit_lbl = newLabel("switch_exit");
            if (n == 0) {                                // empty body: no clauses
                out << "  br label %" << exit_lbl << "\n";
                out << exit_lbl << ":\n";
                return;
            }
            std::vector<std::string> blk(n);
            for (std::size_t k = 0; k < n; k++) blk[k] = newLabel("case");
            std::string default_lbl = exit_lbl;
            for (std::size_t k = 0; k < n; k++) {
                if (!stmt.children[k + 1]->children[0]) {
                    default_lbl = blk[k];
                    break;
                }
            }
            out << "  switch " << llty << " " << sv << ", label %" << default_lbl
                << " [\n";
            for (std::size_t k = 0; k < n; k++) {
                ast::Node const& clause = *stmt.children[k + 1];
                if (clause.children[0]) {
                    out << "    " << llty << " " << clause.children[0]->text
                        << ", label %" << blk[k] << "\n";
                }
            }
            out << "  ]\n";
            bool exit_reachable = (default_lbl == exit_lbl);   // no default clause
            for (std::size_t k = 0; k < n; k++) {
                ast::Node const& body = *stmt.children[k + 1]->children[1];
                out << blk[k] << ":\n";
                LoopCtx swctx{loop ? loop->header_label : std::string(), exit_lbl,
                              loop};
                emitStmt(body, syms, pool, fn_return_type, &swctx, out, diag);
                if (containsBreak(body)) exit_reachable = true;
                if (!endsTerminated(body.children)) {
                    std::string next = (k + 1 < n) ? blk[k + 1] : exit_lbl;
                    out << "  br label %" << next << "\n";
                    if (k + 1 == n) exit_reachable = true;   // last clause falls out
                }
            }
            out << exit_lbl << ":\n";
            if (!exit_reachable) out << "  unreachable\n";
            return;
        }
        case ast::Kind::kCaseClause:
            assert(false && "emitStmt: kCaseClause outside a switch");
            return;
        case ast::Kind::kAugAssignStmt:
            assert(false && "emitStmt: AugAssign survived desugar");
            return;
        case ast::Kind::kFunctionDef:
            // A nested function is lifted to a top-level function (emitted
            // separately by run); it produces no inline code in its host.
            return;
        case ast::Kind::kFunctionDecl:
            // A nested forward declaration produces no code.
            return;
        case ast::Kind::kProgram:
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
        case ast::Kind::kSeqExpr:
        case ast::Kind::kBumpExpr:
        case ast::Kind::kPreIncExpr:
        case ast::Kind::kPostIncExpr:
        case ast::Kind::kParam:
            assert(false && "emitStmt: reached non-statement node kind");
            return;
    }
    assert(false && "emitStmt: unhandled ast::Kind");
    __builtin_unreachable();
}

// "Last statement completes by returning" — a trailing kReturnStmt, or a
// trailing kBlockStmt whose last statement does (recurse). Mirrors classify's
// endsInReturn so the codegen terminator decision matches the upstream check.
bool endsInReturn(std::vector<std::unique_ptr<ast::Node>> const& stmts) {
    if (stmts.empty() || !stmts.back()) return false;
    return endsInReturnNode(*stmts.back());
}

// A single statement guarantees a return: a return, a block whose tail does, or
// an if/else with an else where both arms do. Mirrors classify's endsInReturn so
// the codegen terminator decision matches the upstream return-correctness check.
// Whether a `break` targeting THIS switch/loop appears in `s` — i.e. a break not
// captured by a nested loop/switch. Used to tell whether a switch clause can
// escape to after the switch (so it is not a return-terminator, and its exit
// block is reachable). Does NOT descend into nested capturing constructs.
bool containsBreak(ast::Node const& s) {
    if (s.kind == ast::Kind::kBreakStmt) return true;
    if (s.kind == ast::Kind::kWhileStmt || s.kind == ast::Kind::kDoWhileStmt
        || s.kind == ast::Kind::kForLongStmt || s.kind == ast::Kind::kSwitchStmt) {
        return false;   // a nested loop/switch captures its own breaks
    }
    for (auto const& ch : s.children) if (ch && containsBreak(*ch)) return true;
    return false;
}

// A switch is a return-terminator iff it has a default AND every clause body
// ends in a return AND no clause has a break that escapes to after the switch.
bool switchEndsInReturn(ast::Node const& s);

bool endsInReturnNode(ast::Node const& s) {
    if (s.kind == ast::Kind::kReturnStmt) return true;
    if (s.kind == ast::Kind::kBlockStmt) return endsInReturn(s.children);
    if (s.kind == ast::Kind::kIfStmt && s.children.size() > 2
        && s.children[2]) {
        return endsInReturnNode(*s.children[1])
            && endsInReturnNode(*s.children[2]);
    }
    if (s.kind == ast::Kind::kSwitchStmt) return switchEndsInReturn(s);
    return false;
}

bool switchEndsInReturn(ast::Node const& s) {
    bool has_default = false;
    for (std::size_t i = 1; i < s.children.size(); i++) {
        ast::Node const& clause = *s.children[i];
        if (!clause.children[0]) has_default = true;
        if (!endsInReturnNode(*clause.children[1])
            || containsBreak(*clause.children[1])) {
            return false;
        }
    }
    return has_default;
}

// "This statement always transfers control away" — return / break / continue,
// or a block / both-armed-if whose paths all do. Broader than endsInReturn
// (which return-correctness needs); this drives the br-emit decisions where a
// block must NOT emit a fall-through branch after an already-terminated tail (an
// if-arm's br-to-merge, a while body's back-edge). A while is NOT terminating:
// it always reaches its own exit, so control falls through to what follows it.
bool endsTerminated(std::vector<std::unique_ptr<ast::Node>> const& stmts) {
    if (stmts.empty() || !stmts.back()) return false;
    return endsTerminatedNode(*stmts.back());
}

bool endsTerminatedNode(ast::Node const& s) {
    if (s.kind == ast::Kind::kReturnStmt
        || s.kind == ast::Kind::kBreakStmt
        || s.kind == ast::Kind::kContinueStmt) return true;
    if (s.kind == ast::Kind::kBlockStmt) return endsTerminated(s.children);
    if (s.kind == ast::Kind::kIfStmt && s.children.size() > 2
        && s.children[2]) {
        return endsTerminatedNode(*s.children[1])
            && endsTerminatedNode(*s.children[2]);
    }
    // A switch transfers control away iff no path reaches after it: every clause
    // is a return-terminator (the switchEndsInReturn condition already excludes
    // escaping breaks and requires a default).
    if (s.kind == ast::Kind::kSwitchStmt) return switchEndsInReturn(s);
    return false;
}

// Collect every (non-const) local declaration reachable in this function so its
// alloca can be hoisted to the entry block (see emitFunction). Recurses into the
// statement-bearing children of compound statements — NOT into condition exprs,
// which contain no declarations.
void collectVarDecls(ast::Node const& s, std::vector<ast::Node const*>& out) {
    if (s.kind == ast::Kind::kVarDeclStmt) {
        if (!s.is_const) out.push_back(&s);
        return;
    }
    if (s.kind == ast::Kind::kBlockStmt) {
        for (auto const& ch : s.children) if (ch) collectVarDecls(*ch, out);
        return;
    }
    if (s.kind == ast::Kind::kIfStmt) {
        if (s.children.size() > 1 && s.children[1]) collectVarDecls(*s.children[1], out);
        if (s.children.size() > 2 && s.children[2]) collectVarDecls(*s.children[2], out);
        return;
    }
    if (s.kind == ast::Kind::kWhileStmt || s.kind == ast::Kind::kDoWhileStmt) {
        if (s.children.size() > 1 && s.children[1]) collectVarDecls(*s.children[1], out);
        return;
    }
    if (s.kind == ast::Kind::kForLongStmt) {
        // [0]=cond, [1]=update, [2]=body, [3..]=varlist decls — all hoisted.
        if (s.children.size() > 1 && s.children[1]) collectVarDecls(*s.children[1], out);
        if (s.children.size() > 2 && s.children[2]) collectVarDecls(*s.children[2], out);
        for (std::size_t i = 3; i < s.children.size(); i++) {
            if (s.children[i]) collectVarDecls(*s.children[i], out);
        }
        return;
    }
    if (s.kind == ast::Kind::kSwitchStmt) {
        // [0]=scrutinee (no decls), [1..]=clauses; each clause's [1] is its body.
        for (std::size_t i = 1; i < s.children.size(); i++) {
            if (s.children[i] && s.children[i]->children.size() > 1
                && s.children[i]->children[1]) {
                collectVarDecls(*s.children[i]->children[1], out);
            }
        }
        return;
    }
    // Any other statement kind declares no nested locals.
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
    // A lifted nested function takes one extra `ptr` arg per capture (the host
    // variable's address — by reference).
    for (size_t i = 0; i < fn.captures.size(); i++) {
        if (fn.params.size() + i > 0) out << ", ";
        out << "ptr %cap." << i;
    }
    out << ") {\n";

    SymTab syms;
    // A capture's SymTab "alloca" IS the incoming ptr arg — reads/writes load /
    // store through the host variable's address (no fresh alloca / store).
    for (size_t i = 0; i < fn.captures.size(); i++) {
        std::string cap_llty = llvmTypeFor(fn.capture_types[i],
                                           fn.file_id, fn.tok, diag);
        syms[fn.captures[i]] =
            {std::string("%cap.") + std::to_string(i), cap_llty,
             fn.capture_types[i]};
    }
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
    // Hoist every local's alloca into the entry block. An alloca emitted at its
    // declaration site would re-allocate stack on every pass through an
    // enclosing loop — unbounded growth → stack overflow (the v1 bug). The entry
    // block runs exactly once, so each local is allocated exactly once. Every
    // decl carries a distinct entry id, so the id-suffixed register names never
    // collide across shadowing/scopes; reads resolve by id via the SymTab, and
    // the kVarDeclStmt arm emits only the initializer store.
    std::vector<ast::Node const*> decls;
    for (auto const& s : fn.children) {
        if (s) collectVarDecls(*s, decls);
    }
    for (ast::Node const* d : decls) {
        std::string llty = llvmTypeFor(d->return_type, d->file_id, d->tok, diag);
        std::string regname = std::string("%") + d->name + "."
            + std::to_string(d->resolved_entry_id);
        out << "  " << regname << " = alloca " << llty << "\n";
        syms[d->resolved_entry_id] = {regname, llty, d->return_type};
    }
    for (auto const& s : fn.children) {
        emitStmt(*s, syms, pool, fn.return_type, /*loop=*/nullptr, out, diag);
    }
    // A void function that falls through its body needs an implicit `ret void`
    // terminator — without it the block is unterminated and llc rejects the IR.
    // A non-void function without a trailing return is rejected by classify, so
    // reaching here non-void means that check regressed; assert rather than
    // emit an unterminated block. The guard also means an explicit trailing
    // return won't double-terminate once bare `return;` is supported. A trailing
    // block counts if ITS last statement returns (mirrors classify's recursion).
    if (!endsInReturn(fn.children)) {
        assert(fn.return_type == "void"
            && "emitFunction: non-void function reached codegen without a trailing return");
        out << "  ret void\n";
    }
    out << "}\n";
}

// Collect nested function definitions reachable in a statement subtree (a host
// body), so they can be lifted to top-level functions.
void collectNestedFunctions(ast::Node const& s,
                            std::vector<ast::Node const*>& out) {
    for (auto const& ch : s.children) {
        if (!ch) continue;
        if (ch->kind == ast::Kind::kFunctionDef) out.push_back(ch.get());
        collectNestedFunctions(*ch, out);
    }
}

}  // namespace

void run(ast::Tree const& tree, std::ostream& out, diagnostic::Sink& diag) {
    strings::Pool pool;

    std::ostringstream body;
    std::vector<ast::Node const*> nested;
    for (auto const& n : tree.nodes) {
        if (n->kind != ast::Kind::kProgram) continue;
        for (auto const& fn : n->children) {
            if (fn->kind == ast::Kind::kFunctionDef) {
                emitFunction(*fn, pool, body, diag);
                collectNestedFunctions(*fn, nested);
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
    // Lift each nested function to a top-level function.
    for (ast::Node const* fn : nested) {
        emitFunction(*fn, pool, body, diag);
    }

    out << "target triple = \"x86_64-pc-linux-gnu\"\n\n";
    strings::emit(pool, out);
    if (!pool.texts.empty()) out << "\n";
    out << "declare i32 @printf(ptr, ...)\n\n";
    out << body.str();
}

}  // namespace codegen
