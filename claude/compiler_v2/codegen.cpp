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
                     widen::TypeRef dest_type);

namespace {

// The LLVM type for an interned slids type, read off the structured Type — no
// spelling decomposition. A primitive maps by (cat, bits): float/double for the
// float category, iN otherwise (bool -> i1). Every pointer flavor is `ptr`. A
// fixed array nests REVERSED — the rightmost declared dim is outermost
// (int[3][5] -> [5 x [3 x i32]]) — by wrapping each source-order dim outward.
std::string llvmForRef(widen::TypeRef ref) {
    widen::Type const& t = widen::get(ref);
    switch (t.form) {
        case widen::Type::Form::kPrimitive:
            if (t.cat == widen::Category::kFloat) return t.bits == 32 ? "float" : "double";
            return "i" + std::to_string(t.bits);
        case widen::Type::Form::kVoid:
            return "void";
        case widen::Type::Form::kPointer:
        case widen::Type::Form::kIterator:
        case widen::Type::Form::kAnyptr:
            return "ptr";
        case widen::Type::Form::kArray: {
            // Standard row-major: the FIRST source dim is the OUTERMOST LLVM
            // array, so `int[R][C]` -> `[R x [C x i32]]`. Wrap last-dim-first.
            std::string ll = llvmForRef(t.elem);
            for (auto it = t.dims.rbegin(); it != t.dims.rend(); ++it) {
                ll = "[" + std::to_string(*it) + " x " + ll + "]";
            }
            return ll;
        }
        case widen::Type::Form::kTuple: {
            // Anonymous tuple -> an LLVM literal struct: { llvm(t0), llvm(t1) }.
            std::string s = "{ ";
            for (std::size_t i = 0; i < t.slots.size(); i++) {
                if (i) s += ", ";
                s += llvmForRef(t.slots[i]);
            }
            return s + " }";
        }
        case widen::Type::Form::kAlias:
            return llvmForRef(t.underlying);   // transparent — lower the underlying
        case widen::Type::Form::kNone:
        case widen::Type::Form::kSlid:
            break;   // not lowerable (kNone = no type reached codegen)
    }
    assert(false && "llvmForRef: classify let through an unknown type");
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

bool isFloatType(widen::TypeRef t) {
    widen::TypeKind k;
    return widen::classify(t, k) && k.cat == widen::Category::kFloat;
}

bool isUnsignedType(widen::TypeRef t) {
    widen::TypeKind k;
    return widen::classify(t, k) && k.cat == widen::Category::kUnsignedInt;
}

bool isIteratorType(widen::TypeRef t) {
    return widen::form(t) == widen::Type::Form::kIterator;
}

// The element type of an iterator (`int[]` -> `int`) or reference (`int^` ->
// `int`). kNoType otherwise.
widen::TypeRef pointeeTypeC(widen::TypeRef t) {
    widen::Type const& ty = widen::get(t);
    if (ty.form == widen::Type::Form::kIterator
     || ty.form == widen::Type::Form::kPointer) {
        return ty.pointee;
    }
    return widen::kNoType;
}

// Byte size of a scalar element, for iterator element-stride arithmetic.
int elemBytes(widen::TypeRef t) {
    widen::TypeKind k;
    if (widen::classify(t, k)) return k.bits / 8;
    // Every iterator element today is a scalar, so classify always succeeds.
    // A non-scalar element (a future slid iterator) needs its layout size here.
    assert(false && "elemBytes: non-scalar element needs a layout sizeof");
    __builtin_unreachable();
}

std::string newLabel(char const* tag) {
    static int n = 0;
    return std::string(tag) + "_" + std::to_string(n++);
}

// Truthy coercion: 0-like values (false, 0, 0.0, null ptr) → i1 0; everything
// else → i1 1.
std::string emitToBool(std::string const& val, widen::TypeRef slids_type,
                       std::ostream& out) {
    widen::TypeKind k;
    bool numeric = widen::classify(slids_type, k);
    if (numeric && k.cat == widen::Category::kBool) return val;  // already i1
    if (numeric && k.cat == widen::Category::kFloat) {
        std::string llty = (k.bits == 64) ? "double" : "float";
        std::string tmp = newTmp("tob");
        out << "  " << tmp << " = fcmp une " << llty << " "
            << val << ", 0.0\n";
        return tmp;
    }
    widen::Type::Form bf = widen::form(slids_type);
    if (bf == widen::Type::Form::kPointer || bf == widen::Type::Form::kIterator
     || bf == widen::Type::Form::kAnyptr) {
        std::string tmp = newTmp("tob");
        out << "  " << tmp << " = icmp ne ptr " << val << ", null\n";
        return tmp;
    }
    if (!numeric) {
        assert(false && "emitToBool: unhandled slids type");
        __builtin_unreachable();
    }
    std::string tmp = newTmp("tob");
    out << "  " << tmp << " = icmp ne " << llvmForRef(slids_type) << " "
        << val << ", 0\n";
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

    widen::TypeRef lty = lhs.inferred_type;
    assert(lty != widen::kNoType && "emitLogical: lhs missing inferred_type");
    std::string lv = emitExpr(lhs, syms, pool, out, diag, lty);
    std::string left_bool = emitToBool(lv, lty, out);

    widen::TypeRef rty = rhs.inferred_type;
    assert(rty != widen::kNoType && "emitLogical: rhs missing inferred_type");

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
                      widen::TypeRef dest_type) {
    assert(expr.children.size() == 1 && "emitUnary: UnaryExpr needs 1 child");
    std::string const& op = expr.text;
    ast::Node const& operand = *expr.children[0];

    if (op == "+") {
        return emitExpr(operand, syms, pool, out, diag, dest_type);
    }
    if (op == "-") {
        std::string v = emitExpr(operand, syms, pool, out, diag, dest_type);
        std::string llty = llvmForRef(dest_type);
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
        std::string llty = llvmForRef(dest_type);
        std::string tmp = newTmp("bnot");
        out << "  " << tmp << " = xor " << llty << " " << v << ", -1\n";
        return tmp;
    }
    if (op == "!") {
        widen::TypeRef operand_type = operand.inferred_type;
        assert(operand_type != widen::kNoType && "emitUnary '!': operand missing inferred_type");
        std::string v = emitExpr(operand, syms, pool, out, diag, operand_type);
        std::string llty = llvmForRef(operand_type);
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

// Emit one scalar arith/bitwise instruction on two already-computed values (both
// already at `opty`), returning the result register. Shared by emitBinary's
// scalar path and the slot-wise tuple path. Not for comparisons / shifts / logical.
std::string emitArithInstr(std::string const& op, std::string const& lv,
                           std::string const& rv, widen::TypeRef opty,
                           std::ostream& out) {
    std::string llty = llvmForRef(opty);
    bool flt = isFloatType(opty);
    bool uns = isUnsignedType(opty);
    std::string instr;
    if (flt) {
        if      (op == "+") instr = "fadd";
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
        && "emitArithInstr: no instruction mapped — classify should have rejected "
           "(float bitwise) or covered (all int ops, all float arith)");
    std::string tmp = newTmp("bin");
    out << "  " << tmp << " = " << instr << " " << llty
        << " " << lv << ", " << rv << "\n";
    return tmp;
}

std::string emitBinary(ast::Node const& expr, SymTab const& syms,
                       strings::Pool& pool, std::ostream& out,
                       diagnostic::Sink& diag,
                       widen::TypeRef dest_type) {
    assert(expr.children.size() == 2 && "emitBinary: BinaryExpr needs 2 children");
    std::string const& op = expr.text;
    ast::Node const& lhs = *expr.children[0];
    ast::Node const& rhs = *expr.children[1];

    if (op == "&&" || op == "||" || op == "^^") {
        std::string r = emitLogical(expr, syms, pool, out, diag);
        return widen::convert(r, widen::intern("bool"), dest_type,
                              expr.file_id, expr.tok, out, diag);
    }

    if (op == "<<" || op == ">>") {
        widen::TypeRef lt = lhs.inferred_type;
        widen::TypeRef rt = rhs.inferred_type;
        assert(lt != widen::kNoType && "emitBinary shift: lhs missing inferred_type");
        assert(rt != widen::kNoType && "emitBinary shift: rhs missing inferred_type");
        // Classify already rejected non-integer rhs; nothing to recheck here.

        std::string lv = emitExpr(lhs, syms, pool, out, diag, lt);
        std::string rv = emitExpr(rhs, syms, pool, out, diag, rt);

        if (isFloatType(lt)) {
            // Per fold.sl:128-131: `f << r` ≡ `f * (1<<r)`; `f >> r` ≡ `f / (1<<r)`.
            std::string rllty = llvmForRef(rt);
            std::string fllty = llvmForRef(lt);
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
        std::string lllty = llvmForRef(lt);
        std::string rllty = llvmForRef(rt);
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

    widen::TypeRef opty = expr.op_type;
    assert(opty != widen::kNoType && "emitBinary: BinaryExpr missing op_type");

    // Tuple operand(s): slot-wise op, with a scalar operand BROADCAST to every
    // slot. opty is the result tuple; build it by applying the op per slot
    // (operands converted to the slot's result type). dest_type == opty here, so
    // the aggregate is returned directly.
    if (widen::form(widen::strip(opty)) == widen::Type::Form::kTuple) {
        std::vector<widen::TypeRef> rslots = widen::get(widen::strip(opty)).slots;
        bool ltup = widen::form(widen::strip(lhs.inferred_type))
                    == widen::Type::Form::kTuple;
        bool rtup = widen::form(widen::strip(rhs.inferred_type))
                    == widen::Type::Form::kTuple;
        std::string lagg = emitExpr(lhs, syms, pool, out, diag, lhs.inferred_type);
        std::string ragg = emitExpr(rhs, syms, pool, out, diag, rhs.inferred_type);
        std::string aggty = llvmForRef(opty);
        std::vector<widen::TypeRef> lsl = ltup
            ? widen::get(widen::strip(lhs.inferred_type)).slots
            : std::vector<widen::TypeRef>{};
        std::vector<widen::TypeRef> rsl = rtup
            ? widen::get(widen::strip(rhs.inferred_type)).slots
            : std::vector<widen::TypeRef>{};
        std::string acc = "undef";
        for (std::size_t i = 0; i < rslots.size(); i++) {
            widen::TypeRef st = rslots[i];
            std::string lv;
            if (ltup) {
                std::string ex = newTmp("lx");
                out << "  " << ex << " = extractvalue "
                    << llvmForRef(lhs.inferred_type) << " " << lagg << ", " << i << "\n";
                lv = widen::convert(ex, lsl[i], st, expr.file_id, expr.tok, out, diag);
            } else {
                lv = widen::convert(lagg, lhs.inferred_type, st,
                                    expr.file_id, expr.tok, out, diag);
            }
            std::string rv;
            if (rtup) {
                std::string ex = newTmp("rx");
                out << "  " << ex << " = extractvalue "
                    << llvmForRef(rhs.inferred_type) << " " << ragg << ", " << i << "\n";
                rv = widen::convert(ex, rsl[i], st, expr.file_id, expr.tok, out, diag);
            } else {
                rv = widen::convert(ragg, rhs.inferred_type, st,
                                    expr.file_id, expr.tok, out, diag);
            }
            std::string r = emitArithInstr(op, lv, rv, st, out);
            std::string tmp = newTmp("tup");
            out << "  " << tmp << " = insertvalue " << aggty << " " << acc << ", "
                << llvmForRef(st) << " " << r << ", " << i << "\n";
            acc = tmp;
        }
        return acc;
    }

    // Iterator arithmetic: `iter ± int` (GEP by element) and `iter - iter`
    // (element-count difference). op_type carries the iterator type; the generic
    // operand emit below would mis-convert the integer operand, so branch first.
    // (Comparisons keep the generic path — they icmp the raw pointers.)
    if (isIteratorType(opty) && (op == "+" || op == "-")) {
        widen::TypeRef elem = pointeeTypeC(opty);
        std::string elem_ll = llvmForRef(elem);
        bool lit = isIteratorType(lhs.inferred_type);
        bool rit = isIteratorType(rhs.inferred_type);
        if (op == "-" && lit && rit) {
            // (a - b) / sizeof(element) -> element count.
            std::string a = emitExpr(lhs, syms, pool, out, diag,
                                     lhs.inferred_type);
            std::string b = emitExpr(rhs, syms, pool, out, diag,
                                     rhs.inferred_type);
            std::string ai = newTmp("p2i"), bi = newTmp("p2i");
            out << "  " << ai << " = ptrtoint ptr " << a << " to i64\n";
            out << "  " << bi << " = ptrtoint ptr " << b << " to i64\n";
            std::string byte = newTmp("psub");
            out << "  " << byte << " = sub i64 " << ai << ", " << bi << "\n";
            std::string d = newTmp("pdiv");
            out << "  " << d << " = sdiv i64 " << byte << ", "
                << elemBytes(elem) << "\n";
            return widen::convert(d, widen::intern("intptr"), dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        // iter ± int: GEP the iterator by (signed) the integer count.
        ast::Node const& itnode = lit ? lhs : rhs;
        ast::Node const& intnode = lit ? rhs : lhs;
        std::string ptr = emitExpr(itnode, syms, pool, out, diag,
                                   itnode.inferred_type);
        std::string idx = emitExpr(intnode, syms, pool, out, diag, widen::intern("int64"));
        if (op == "-") {
            std::string neg = newTmp("pneg");
            out << "  " << neg << " = sub i64 0, " << idx << "\n";
            idx = neg;
        }
        std::string gep = newTmp("itadd");
        out << "  " << gep << " = getelementptr " << elem_ll << ", ptr " << ptr
            << ", i64 " << idx << "\n";
        return gep;
    }

    std::string lv = emitExpr(lhs, syms, pool, out, diag, opty);
    std::string rv = emitExpr(rhs, syms, pool, out, diag, opty);

    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        std::string llty = llvmForRef(opty);
        bool flt = isFloatType(opty);
        // Pointer comparisons (reference / iterator / anyptr) compare addresses
        // as unsigned.
        widen::Type::Form of = widen::form(opty);
        bool ptr_cmp = of == widen::Type::Form::kPointer
            || of == widen::Type::Form::kIterator
            || of == widen::Type::Form::kAnyptr;
        bool uns = isUnsignedType(opty) || opty == widen::intern("bool") || ptr_cmp;
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
        return widen::convert(tmp, widen::intern("bool"), dest_type,
                              expr.file_id, expr.tok, out, diag);
    }

    std::string tmp = emitArithInstr(op, lv, rv, opty, out);
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
    // Passing a VALUE to a reference param (`dump(#x)` — an rvalue tuple to a
    // `(...)^`) materializes it in a temp alloca and passes the address. Excludes
    // a pointer-like arg (an iterator demoting to a reference passes its own
    // pointer, not a fresh temp).
    auto isValueByRef = [&](ast::Node const& arg, widen::TypeRef dest) {
        if (widen::form(widen::strip(dest)) != widen::Type::Form::kPointer)
            return false;
        if (arg.inferred_type == widen::kNoType) return false;
        widen::Type::Form af = widen::form(widen::strip(arg.inferred_type));
        return af != widen::Type::Form::kPointer
            && af != widen::Type::Form::kIterator
            && af != widen::Type::Form::kAnyptr;
    };
    // If any arg materializes a temp alloca, bracket the call in stacksave/
    // stackrestore so the slot is freed each time — a materializing call in a
    // loop reuses the stack region instead of leaking an alloca per iteration.
    bool materializes = false;
    for (size_t i = 0; i < call.children.size(); i++)
        if (isValueByRef(*call.children[i], call.param_types[i])) {
            materializes = true;
            break;
        }
    std::string sp;
    if (materializes) {
        sp = newTmp("sp");
        out << "  " << sp << " = call ptr @llvm.stacksave.p0()\n";
    }
    std::vector<std::pair<std::string, std::string>> arg_vals;
    arg_vals.reserve(call.children.size());
    for (size_t i = 0; i < call.children.size(); i++) {
        ast::Node const& arg = *call.children[i];
        widen::TypeRef dest = call.param_types[i];
        if (isValueByRef(arg, dest)) {
            widen::TypeRef pointee = widen::get(widen::strip(dest)).pointee;
            std::string pll = llvmForRef(pointee);
            std::string v = emitExpr(arg, syms, pool, out, diag, pointee);
            std::string slot = newTmp("argtmp");
            out << "  " << slot << " = alloca " << pll << "\n";
            out << "  store " << pll << " " << v << ", ptr " << slot << "\n";
            arg_vals.push_back({"ptr", slot});
            continue;
        }
        std::string val = emitExpr(arg, syms, pool, out, diag, dest);
        std::string llty = llvmForRef(dest);
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
    std::string ret_llty = llvmForRef(call.return_type);
    std::string result;
    out << "  ";
    if (widen::form(call.return_type) != widen::Type::Form::kVoid) {
        result = newTmp("call");
        out << result << " = ";
    }
    out << "call " << ret_llty << " @" << call.name << "(";
    for (size_t i = 0; i < arg_vals.size(); i++) {
        if (i > 0) out << ", ";
        out << arg_vals[i].first << " " << arg_vals[i].second;
    }
    out << ")\n";
    if (materializes) {
        out << "  call void @llvm.stackrestore.p0(ptr " << sp << ")\n";
    }
    return result;
}

// Compute the address of an array element from a (possibly nested) kIndexExpr.
// Walks the subscript chain to the base array, then emits ONE getelementptr.
// The chain is collected outermost-first, which matches the LLVM nesting (outer
// dim first) — so the source's leftmost (inner) index becomes the LAST GEP
// index, exactly the reversed-dimension layout.
std::string emitElementAddr(ast::Node const& index_expr, SymTab const& syms,
                            strings::Pool& pool, std::ostream& out,
                            diagnostic::Sink& diag) {
    std::vector<ast::Node const*> chain;   // outermost .. innermost kIndexExpr
    ast::Node const* node = &index_expr;
    while (node->kind == ast::Kind::kIndexExpr) {
        chain.push_back(node);
        node = node->children[0].get();
    }
    // Iterator base: the base expression is a `ptr` value (the sequence start);
    // GEP by element type. Iterators are one-dimensional (a single subscript).
    if (isIteratorType(node->inferred_type)) {
        std::string ptr = emitExpr(*node, syms, pool, out, diag,
                                   node->inferred_type);
        std::string idx = emitExpr(*chain.back()->children[1], syms, pool, out,
                                   diag, widen::intern("int64"));
        std::string elem_ll = llvmForRef(pointeeTypeC(node->inferred_type));
        std::string gep = newTmp("elt");
        out << "  " << gep << " = getelementptr " << elem_ll << ", ptr " << ptr
            << ", i64 " << idx << "\n";
        return gep;
    }
    // Tuple-slot base: GEP the struct field at the (constant) slot index.
    if (widen::form(widen::strip(node->inferred_type))
        == widen::Type::Form::kTuple) {
        assert(node->kind == ast::Kind::kIdentExpr && node->resolved_entry_id >= 0
            && "emitElementAddr: tuple slot base must be a variable");
        auto tit = syms.find(node->resolved_entry_id);
        assert(tit != syms.end() && "emitElementAddr: tuple not in SymTab");
        long k = std::strtol(chain.back()->children[1]->text.c_str(), nullptr, 10);
        std::string gep = newTmp("slot");
        out << "  " << gep << " = getelementptr inbounds " << tit->second.llvm_type
            << ", ptr " << tit->second.alloca_name << ", i32 0, i32 " << k << "\n";
        return gep;
    }
    // Fixed-size array base: GEP into the aggregate at its alloca.
    assert(node->kind == ast::Kind::kIdentExpr && node->resolved_entry_id >= 0
        && "emitElementAddr: array subscript base must be a variable");
    auto it = syms.find(node->resolved_entry_id);
    assert(it != syms.end() && "emitElementAddr: array not in SymTab");
    // Every dimension must be indexed — a partial index (`grid[0]` on a 2-D
    // array) would yield a sub-array, which has no scalar value to load/store.
    int dims = (int)widen::get(it->second.slids_type).dims.size();
    if ((int)chain.size() != dims) {
        diagnostic::report(diag, {index_expr.file_id, index_expr.tok,
            "An array subscript must index every dimension.", {}});
        return "null";
    }
    // Evaluate every index FIRST (each emits its own instructions), then emit
    // the single GEP line — otherwise an index's loads land mid-GEP. Standard
    // row-major: the leftmost SOURCE subscript is the OUTERMOST GEP index, so
    // walk the chain innermost-first (its reverse) and evaluate left-to-right.
    std::vector<std::string> idx_vals;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        idx_vals.push_back(
            emitExpr(*(*it)->children[1], syms, pool, out, diag, widen::intern("int64")));
    }
    std::string gep = newTmp("elt");
    out << "  " << gep << " = getelementptr inbounds " << it->second.llvm_type
        << ", ptr " << it->second.alloca_name << ", i64 0";
    for (auto const& iv : idx_vals) out << ", i64 " << iv;
    out << "\n";
    return gep;
}

// True if `ty` is, or (for a tuple) transitively contains, a pointer / iterator
// leaf — i.e. a move must null something inside it.
bool typeHasPointer(widen::TypeRef ty) {
    widen::TypeRef s = widen::strip(ty);
    widen::Type::Form f = widen::form(s);
    if (f == widen::Type::Form::kPointer || f == widen::Type::Form::kIterator) {
        return true;
    }
    if (f == widen::Type::Form::kTuple) {
        for (widen::TypeRef slot : widen::get(s).slots) {
            if (typeHasPointer(slot)) return true;
        }
    }
    return false;
}

// An lvalue expression's address: a bare variable is its alloca; an index is
// emitElementAddr; a deref's address is the pointer value it loads.
std::string emitLvalueAddr(ast::Node const& lv, SymTab const& syms,
                           strings::Pool& pool, std::ostream& out,
                           diagnostic::Sink& diag) {
    if (lv.kind == ast::Kind::kIdentExpr) {
        auto it = syms.find(lv.resolved_entry_id);
        assert(it != syms.end() && "emitLvalueAddr: ident not in SymTab");
        return it->second.alloca_name;
    }
    if (lv.kind == ast::Kind::kIndexExpr) {
        return emitElementAddr(lv, syms, pool, out, diag);
    }
    assert(lv.kind == ast::Kind::kDerefExpr && "emitLvalueAddr: unsupported lvalue");
    ast::Node const& ptr_expr = *lv.children[0];
    return emitExpr(ptr_expr, syms, pool, out, diag, ptr_expr.inferred_type);
}

// True for a move/swap operand whose storage can be addressed (so a move can
// null its pointer leaves). A literal / addr-of / call / tuple-literal is an
// rvalue with no storage — a move from one is a pure copy.
bool isAstLvalue(ast::Node const& n) {
    return n.kind == ast::Kind::kIdentExpr
        || n.kind == ast::Kind::kIndexExpr
        || n.kind == ast::Kind::kDerefExpr;
}

// Null every addressable pointer leaf reachable from `addr` (a value of type
// `ty`): a pointer / iterator leaf gets `store ptr null`; a tuple recurses into
// each slot that transitively holds a pointer; a primitive is left untouched.
// The address-level GEP recursion handles arbitrarily nested tuples (the move
// "fancy case").
void emitNullLeaves(std::string const& addr, widen::TypeRef ty,
                    std::ostream& out) {
    widen::TypeRef s = widen::strip(ty);
    widen::Type::Form f = widen::form(s);
    if (f == widen::Type::Form::kPointer || f == widen::Type::Form::kIterator) {
        out << "  store ptr null, ptr " << addr << "\n";
        return;
    }
    if (f == widen::Type::Form::kTuple) {
        std::string tll = llvmForRef(s);
        std::vector<widen::TypeRef> const& slots = widen::get(s).slots;
        for (std::size_t i = 0; i < slots.size(); i++) {
            if (!typeHasPointer(slots[i])) continue;
            std::string gep = newTmp("nleaf");
            out << "  " << gep << " = getelementptr inbounds " << tll
                << ", ptr " << addr << ", i32 0, i32 " << i << "\n";
            emitNullLeaves(gep, slots[i], out);
        }
    }
    // Any other form is a leaf with nothing to null: a primitive (the common
    // case — reached, intended), or — once they can be tuple slots — an array of
    // pointers or a class with pointer fields. Those are NOT constructible inside
    // a tuple today (an array type as a slot is a parse error; classes are Phase
    // 5), so this falls through cleanly; revisit when they land (an array would
    // need a per-element walk, a class its move operator). user notified, accepts
    // state.
}

}  // namespace

std::string emitExpr(ast::Node const& expr, SymTab const& syms,
                     strings::Pool& pool, std::ostream& out,
                     diagnostic::Sink& diag,
                     widen::TypeRef dest_type) {
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
            // A 32-bit float dest emits the hex bit-pattern (classify sees through
            // a float alias); float64 / no context keeps the decimal form.
            widen::TypeKind fk;
            if (widen::classify(dest_type, fk) && fk.cat == widen::Category::kFloat
                && fk.bits == 32) {
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
        case ast::Kind::kNullptrLiteral:
            // The typeless null; every pointer type is LLVM `ptr`.
            return "null";
        case ast::Kind::kAddrOfExpr: {
            // `^lvalue` — its address (a ptr). For a bare variable that is its
            // alloca register; for `^arr[i]` it is the element GEP (an iterator).
            assert(expr.children.size() == 1 && "kAddrOfExpr needs 1 operand");
            ast::Node const& operand = *expr.children[0];
            if (operand.kind == ast::Kind::kIndexExpr) {
                return emitElementAddr(operand, syms, pool, out, diag);
            }
            assert(operand.kind == ast::Kind::kIdentExpr
                && operand.resolved_entry_id >= 0
                && "kAddrOfExpr: operand must be a resolved variable");
            auto it = syms.find(operand.resolved_entry_id);
            assert(it != syms.end() && "kAddrOfExpr: operand not in SymTab");
            return it->second.alloca_name;
        }
        case ast::Kind::kIndexExpr: {
            // A tuple slot read `tup[k]`: emit the aggregate value, extract slot k
            // (classify guaranteed k is a constant in range).
            ast::Node const& ibase = *expr.children[0];
            if (widen::form(widen::strip(ibase.inferred_type))
                == widen::Type::Form::kTuple) {
                std::string agg = emitExpr(ibase, syms, pool, out, diag,
                                           ibase.inferred_type);
                long idx = std::strtol(expr.children[1]->text.c_str(), nullptr, 10);
                std::string llbase = llvmForRef(ibase.inferred_type);
                std::string tmp = newTmp("slot");
                out << "  " << tmp << " = extractvalue " << llbase << " " << agg
                    << ", " << idx << "\n";
                return widen::convert(tmp, expr.inferred_type, dest_type,
                                      expr.file_id, expr.tok, out, diag);
            }
            // `arr[i]` rvalue: address the element, load it.
            std::string addr = emitElementAddr(expr, syms, pool, out, diag);
            std::string llty = llvmForRef(expr.inferred_type);
            std::string tmp = newTmp("idx");
            out << "  " << tmp << " = load " << llty << ", ptr " << addr << "\n";
            return widen::convert(tmp, expr.inferred_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        case ast::Kind::kTupleExpr: {
            // Build the literal struct `{ t0, t1, ... }` by inserting each slot
            // value into an undef aggregate (value semantics).
            std::string llty = llvmForRef(expr.inferred_type);
            std::vector<widen::TypeRef> slots =
                widen::get(widen::strip(expr.inferred_type)).slots;
            std::string acc = "undef";
            for (std::size_t i = 0; i < expr.children.size(); i++) {
                std::string v = emitExpr(*expr.children[i], syms, pool, out, diag,
                                         slots[i]);
                std::string tmp = newTmp("tup");
                out << "  " << tmp << " = insertvalue " << llty << " " << acc
                    << ", " << llvmForRef(slots[i]) << " " << v << ", " << i << "\n";
                acc = tmp;
            }
            return acc;
        }
        case ast::Kind::kDerefExpr: {
            // `ptr^` rvalue: the operand yields the pointer value (the address);
            // load the pointee from it.
            assert(expr.children.size() == 1 && "kDerefExpr needs 1 operand");
            ast::Node const& operand = *expr.children[0];
            std::string addr = emitExpr(operand, syms, pool, out, diag,
                                        operand.inferred_type);
            std::string llty = llvmForRef(expr.inferred_type);
            std::string tmp = newTmp("deref");
            out << "  " << tmp << " = load " << llty << ", ptr " << addr << "\n";
            return widen::convert(tmp, expr.inferred_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        case ast::Kind::kNewExpr: {
            // new T -> malloc(sizeof(T)); new T[n] -> malloc(n * sizeof(T));
            // new(addr) T[n] -> the address itself (no allocation). All yield a
            // `ptr` (the result is T^ / T[]). Phase 4: primitives, so no ctor runs.
            std::string p;
            if (expr.children[1]) {            // placement: result is the address
                ast::Node const& addr = *expr.children[1];
                p = emitExpr(addr, syms, pool, out, diag, addr.inferred_type);
            } else {
                long long elem = widen::typeByteSize(expr.return_type);
                // classify rejected an unsized element ("Cannot allocate") and
                // main short-circuits, so a -1 here means a missed gate — assert
                // rather than emit malloc(i64 -1).
                assert(elem >= 0 && "kNewExpr: unsized element reached codegen");
                std::string bytes;
                if (expr.children[0]) {        // array: n * elem-size
                    std::string n = emitExpr(*expr.children[0], syms, pool, out,
                                             diag, widen::intern("int64"));
                    std::string mul = newTmp("nbytes");
                    out << "  " << mul << " = mul i64 " << n << ", "
                        << elem << "\n";
                    bytes = mul;
                } else {
                    bytes = std::to_string(elem);
                }
                p = newTmp("new");
                out << "  " << p << " = call ptr @malloc(i64 " << bytes << ")\n";
            }
            return widen::convert(p, expr.inferred_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        case ast::Kind::kCastExpr: {
            // `<Type^> operand` — reinterpret. Emit the operand at its own type,
            // convert it to the cast target (a ptrtoint/inttoptr only at the
            // intptr boundary; ptr↔ptr is a no-op under opaque `ptr`), then flex
            // into the surrounding dest_type (target → dest is itself a no-op for
            // a pointer lvalue, or a ptr/intptr bridge).
            assert(expr.children.size() == 1 && "kCastExpr needs 1 operand");
            ast::Node const& operand = *expr.children[0];
            std::string v = emitExpr(operand, syms, pool, out, diag,
                                     operand.inferred_type);
            v = widen::convert(v, operand.inferred_type, expr.inferred_type,
                               expr.file_id, expr.tok, out, diag);
            return widen::convert(v, expr.inferred_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        case ast::Kind::kConvertExpr: {
            // `(Type=operand)` value conversion. Emit the operand at its own type,
            // change the bits via the explicit grid (trunc/ext/fp<->int/sign
            // reinterpret/nonzero test), then flex the target into the
            // surrounding dest_type (a no-op or implicit widen).
            assert(expr.children.size() == 1 && "kConvertExpr needs 1 operand");
            ast::Node const& operand = *expr.children[0];
            std::string v = emitExpr(operand, syms, pool, out, diag,
                                     operand.inferred_type);
            v = widen::convertExplicit(v, operand.inferred_type, expr.inferred_type, out);
            return widen::convert(v, expr.inferred_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        case ast::Kind::kUnaryExpr:
            return emitUnary(expr, syms, pool, out, diag, dest_type);
        case ast::Kind::kBinaryExpr:
            return emitBinary(expr, syms, pool, out, diag, dest_type);
        case ast::Kind::kCallExpr: {
            assert(widen::form(expr.return_type) != widen::Type::Form::kVoid
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
                    emitExpr(*expr.children[i], syms, pool, out, diag, widen::kNoType);
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
            // An iterator steps by one ELEMENT: load the ptr, GEP ±1, store.
            if (isIteratorType(it->second.slids_type)) {
                std::string cur = newTmp("itld");
                out << "  " << cur << " = load ptr, ptr "
                    << it->second.alloca_name << "\n";
                std::string elem_ll = llvmForRef(pointeeTypeC(it->second.slids_type));
                std::string nv = newTmp("itinc");
                out << "  " << nv << " = getelementptr " << elem_ll << ", ptr "
                    << cur << ", i64 " << (expr.text == "++" ? "1" : "-1")
                    << "\n";
                out << "  store ptr " << nv << ", ptr "
                    << it->second.alloca_name << "\n";
                return nv;
            }
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
        case ast::Kind::kStoreStmt:
        case ast::Kind::kMoveStmt:
        case ast::Kind::kSwapStmt:
        case ast::Kind::kDestructureStmt:
        case ast::Kind::kDeleteStmt:
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

// Collect a tuple literal's leaf value-nodes in storage order (recurse nested).
void collectTupleLeafNodesAst(ast::Node const& n,
                              std::vector<ast::Node const*>& out) {
    if (n.kind == ast::Kind::kTupleExpr) {
        for (auto const& c : n.children) if (c) collectTupleLeafNodesAst(*c, out);
    } else {
        out.push_back(&n);
    }
}

// Initialize / assign an array from a tuple LITERAL, element-wise. A homogeneous
// (possibly nested) tuple's leaves in storage order map onto the array's flat
// slots; each leaf is emitted in the element type's context (a literal flexes, a
// typed value widens) and stored at flat offset i — no tuple aggregate is built.
void emitArrayFromTuple(std::string const& alloca_name, widen::TypeRef arrType,
                        ast::Node const& rhs, SymTab const& syms,
                        strings::Pool& pool, std::ostream& out,
                        diagnostic::Sink& diag) {
    widen::TypeRef elem = widen::get(widen::strip(arrType)).elem;
    std::string elem_ll = llvmForRef(elem);
    std::vector<ast::Node const*> leaves;
    collectTupleLeafNodesAst(rhs, leaves);
    for (std::size_t i = 0; i < leaves.size(); i++) {
        std::string v = emitExpr(*leaves[i], syms, pool, out, diag, elem);
        std::string gep = newTmp("aelt");
        out << "  " << gep << " = getelementptr " << elem_ll << ", ptr "
            << alloca_name << ", i64 " << i << "\n";
        out << "  store " << elem_ll << " " << v << ", ptr " << gep << "\n";
    }
}

void emitStmt(ast::Node const& stmt, SymTab& syms,
              strings::Pool& pool,
              widen::TypeRef fn_return_type,
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
                if (widen::form(widen::strip(it->second.slids_type))
                        == widen::Type::Form::kArray
                    && stmt.children[0]->kind == ast::Kind::kTupleExpr) {
                    emitArrayFromTuple(it->second.alloca_name, it->second.slids_type,
                                       *stmt.children[0], syms, pool, out, diag);
                    return;
                }
                std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                           it->second.slids_type);
                out << "  store " << it->second.llvm_type << " " << val
                    << ", ptr " << it->second.alloca_name << "\n";
                // A move-init (`T x <-- y`) nulls the source's pointer leaves
                // after the copy, leaving the source in a valid state. An rvalue
                // source has no storage to null.
                if (stmt.move_init && isAstLvalue(*stmt.children[0])) {
                    std::string src = emitLvalueAddr(*stmt.children[0], syms, pool,
                                                     out, diag);
                    emitNullLeaves(src, stmt.children[0]->inferred_type, out);
                }
            }
            return;
        }
        case ast::Kind::kAssignStmt: {
            assert(stmt.resolved_entry_id >= 0
                && "kAssignStmt: classify did not stamp resolved_entry_id");
            auto it = syms.find(stmt.resolved_entry_id);
            assert(it != syms.end()
                && "kAssignStmt: entry not in SymTab (alloca never emitted?)");
            if (widen::form(widen::strip(it->second.slids_type))
                    == widen::Type::Form::kArray
                && stmt.children[0]->kind == ast::Kind::kTupleExpr) {
                emitArrayFromTuple(it->second.alloca_name, it->second.slids_type,
                                   *stmt.children[0], syms, pool, out, diag);
                return;
            }
            std::string val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                       it->second.slids_type);
            out << "  store " << it->second.llvm_type << " " << val
                << ", ptr " << it->second.alloca_name << "\n";
            return;
        }
        case ast::Kind::kStoreStmt: {
            // Store through an lvalue expression: `arr[i] = rhs` (index) or
            // `ref^ = rhs` (deref). Compute the destination address, then store
            // the rhs flexed to the element/pointee type.
            assert(stmt.children.size() == 2 && "kStoreStmt needs lvalue + rhs");
            ast::Node const& lvalue = *stmt.children[0];
            widen::TypeRef elem = lvalue.inferred_type;
            std::string addr;
            if (lvalue.kind == ast::Kind::kIndexExpr) {
                addr = emitElementAddr(lvalue, syms, pool, out, diag);
            } else {
                assert(lvalue.kind == ast::Kind::kDerefExpr
                    && "kStoreStmt: lvalue must be a deref or index");
                ast::Node const& ptr_expr = *lvalue.children[0];
                addr = emitExpr(ptr_expr, syms, pool, out, diag,
                                ptr_expr.inferred_type);
            }
            std::string val = emitExpr(*stmt.children[1], syms, pool, out, diag,
                                       elem);
            std::string llty = llvmForRef(elem);
            out << "  store " << llty << " " << val << ", ptr " << addr << "\n";
            return;
        }
        case ast::Kind::kMoveStmt: {
            // `a <-- b;` — copy b into a (widen / pointer-cast via emitExpr+store,
            // exactly an assignment), then null every addressable pointer leaf of
            // b so the source is left valid. An rvalue source has no leaves.
            assert(stmt.children.size() == 2 && "kMoveStmt needs lhs + rhs");
            ast::Node const& lhs = *stmt.children[0];
            ast::Node const& rhs = *stmt.children[1];
            std::string dst = emitLvalueAddr(lhs, syms, pool, out, diag);
            std::string val = emitExpr(rhs, syms, pool, out, diag, lhs.inferred_type);
            out << "  store " << llvmForRef(lhs.inferred_type) << " " << val
                << ", ptr " << dst << "\n";
            if (isAstLvalue(rhs)) {
                std::string src = emitLvalueAddr(rhs, syms, pool, out, diag);
                emitNullLeaves(src, rhs.inferred_type, out);
            }
            return;
        }
        case ast::Kind::kSwapStmt: {
            // `a <--> b;` — exchange two same-type lvalues via SSA temporaries
            // (no stack temp). A whole-value load/store handles tuples too. Both
            // addresses and loads precede either store, so an aliased swap is safe.
            assert(stmt.children.size() == 2 && "kSwapStmt needs two operands");
            ast::Node const& a = *stmt.children[0];
            ast::Node const& b = *stmt.children[1];
            std::string ll = llvmForRef(a.inferred_type);
            std::string addr_a = emitLvalueAddr(a, syms, pool, out, diag);
            std::string addr_b = emitLvalueAddr(b, syms, pool, out, diag);
            std::string va = newTmp("swap");
            std::string vb = newTmp("swap");
            out << "  " << va << " = load " << ll << ", ptr " << addr_a << "\n";
            out << "  " << vb << " = load " << ll << ", ptr " << addr_b << "\n";
            out << "  store " << ll << " " << vb << ", ptr " << addr_a << "\n";
            out << "  store " << ll << " " << va << ", ptr " << addr_b << "\n";
            return;
        }
        case ast::Kind::kDestructureStmt: {
            // (a, b, ) = tuple. Evaluate the rhs aggregate once, then extract
            // each slot and store it to its target (a null child is skipped).
            ast::Node const& rhs = *stmt.children[0];
            std::string agg = emitExpr(rhs, syms, pool, out, diag,
                                       rhs.inferred_type);
            std::string llty = llvmForRef(rhs.inferred_type);
            for (std::size_t i = 1; i < stmt.children.size(); i++) {
                ast::Node const* tgt = stmt.children[i].get();
                if (!tgt) continue;   // skipped slot
                auto it = syms.find(tgt->resolved_entry_id);
                assert(it != syms.end()
                    && "kDestructureStmt: target not in SymTab");
                std::string slot = newTmp("dslot");
                out << "  " << slot << " = extractvalue " << llty << " " << agg
                    << ", " << (i - 1) << "\n";
                out << "  store " << it->second.llvm_type << " " << slot
                    << ", ptr " << it->second.alloca_name << "\n";
            }
            return;
        }
        case ast::Kind::kDeleteStmt: {
            // delete p; — load the pointer, free it, store null back. resolve
            // guaranteed the operand is a variable; classify, a pointer type.
            ast::Node const& operand = *stmt.children[0];
            assert(operand.kind == ast::Kind::kIdentExpr
                && operand.resolved_entry_id >= 0
                && "kDeleteStmt: operand must be a resolved variable");
            auto it = syms.find(operand.resolved_entry_id);
            assert(it != syms.end() && "kDeleteStmt: operand not in SymTab");
            std::string p = newTmp("del");
            out << "  " << p << " = load ptr, ptr " << it->second.alloca_name
                << "\n";
            out << "  call void @free(ptr " << p << ")\n";
            out << "  store ptr null, ptr " << it->second.alloca_name << "\n";
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
            emitExpr(*stmt.children[0], syms, pool, out, diag, widen::kNoType);
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
            std::string llty = llvmForRef(fn_return_type);
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
            // A non-completing loop (constant-true, no break) never reaches exit.
            if (stmt.non_completing) out << "  unreachable\n";
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
            // A non-completing loop (constant-true, no break) never reaches exit.
            if (stmt.non_completing) out << "  unreachable\n";
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
            // A non-completing loop (constant-true, no break) never reaches exit.
            if (stmt.non_completing) out << "  unreachable\n";
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
            std::string llty = llvmForRef(scrut.inferred_type);
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
        case ast::Kind::kNullptrLiteral:
        case ast::Kind::kIdentExpr:
        case ast::Kind::kUnaryExpr:
        case ast::Kind::kBinaryExpr:
        case ast::Kind::kAddrOfExpr:
        case ast::Kind::kDerefExpr:
        case ast::Kind::kIndexExpr:
        case ast::Kind::kTupleExpr:
        case ast::Kind::kCastExpr:
        case ast::Kind::kConvertExpr:
        case ast::Kind::kNewExpr:
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
    // A non-completing loop (resolve-flagged constant-true, no break) never falls
    // through — like a return, it terminates control. Its exit block already gets
    // an `unreachable`, so the function epilogue must not append a `ret`.
    if (s.kind == ast::Kind::kWhileStmt || s.kind == ast::Kind::kDoWhileStmt
        || s.kind == ast::Kind::kForLongStmt) {
        return s.non_completing;
    }
    return false;
}

bool switchEndsInReturn(ast::Node const& s) {
    // Mirrors classify: a switch terminates iff it has a default, no clause has an
    // escaping break, and the LAST clause's body ends in a return (C-style
    // fall-through carries a stacked/non-returning clause into that final return).
    bool has_default = false;
    for (std::size_t i = 1; i < s.children.size(); i++) {
        ast::Node const& clause = *s.children[i];
        if (!clause.children[0]) has_default = true;
        if (containsBreak(*clause.children[1])) return false;
    }
    if (!has_default) return false;
    return endsInReturnNode(*s.children.back()->children[1]);
}

// "This statement always transfers control away" — return / break / continue,
// or a block / both-armed-if whose paths all do. Broader than endsInReturn
// (which return-correctness needs); this drives the br-emit decisions where a
// block must NOT emit a fall-through branch after an already-terminated tail (an
// if-arm's br-to-merge, a while body's back-edge). A COMPLETING loop is NOT
// terminating: it reaches its own exit, so control falls through to what follows
// it. A NON-completing loop (constant-true, no break) IS terminating — its exit
// block is `unreachable`, so no fall-through branch may follow it.
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
    // A non-completing loop's exit is `unreachable` — control never falls past it.
    if (s.kind == ast::Kind::kWhileStmt || s.kind == ast::Kind::kDoWhileStmt
        || s.kind == ast::Kind::kForLongStmt) {
        return s.non_completing;
    }
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
    std::string ret_llty = llvmForRef(fn.return_type);
    out << "define " << ret_llty << " @" << fn.name << "(";
    for (size_t i = 0; i < fn.params.size(); i++) {
        ast::Node const& p = *fn.params[i];
        if (i > 0) out << ", ";
        std::string p_llty = llvmForRef(p.return_type);
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
        std::string cap_llty = llvmForRef(fn.capture_types[i]);
        syms[fn.captures[i]] =
            {std::string("%cap.") + std::to_string(i), cap_llty,
             fn.capture_types[i]};
    }
    // Alloca + store-in each param so the body can read/write it like a local.
    // Register under the param's resolved_entry_id (stamped by classify's
    // body-frame seeding).
    for (size_t i = 0; i < fn.params.size(); i++) {
        ast::Node const& p = *fn.params[i];
        std::string p_llty = llvmForRef(p.return_type);
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
        std::string llty = llvmForRef(d->return_type);
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
        assert(widen::form(fn.return_type) == widen::Type::Form::kVoid
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
    out << "declare i32 @printf(ptr, ...)\n";
    out << "declare ptr @malloc(i64)\n";
    out << "declare void @free(ptr)\n";
    // Save/restore the stack pointer around a call that materializes an rvalue
    // arg in a temp alloca, so a such a call in a loop reuses the slot instead of
    // leaking a fresh alloca each iteration.
    out << "declare ptr @llvm.stacksave.p0()\n";
    out << "declare void @llvm.stackrestore.p0(ptr)\n\n";
    out << body.str();
}

}  // namespace codegen
