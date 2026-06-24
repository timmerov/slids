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

// Forward: build an `[N x T]` aggregate VALUE from a tuple literal (for an
// array-typed tuple slot). Defined below, after collectArrayElementNodesAst.
std::string emitArrayLiteralValue(widen::TypeRef arrType, ast::Node const& rhs,
                                  SymTab const& syms, strings::Pool& pool,
                                  std::ostream& out, diagnostic::Sink& diag);

// Forward: ctor/dtor hooks + the needs predicate (defined below) — new T(args)
// runs the ctor at the freshly-allocated object, delete runs the dtor before free,
// and new[]/delete[] gate the count cookie on whether the element needs a dtor.
void emitConstructHooks(std::string const& addr, widen::TypeRef type,
                        std::ostream& out);
bool typeNeedsHook(widen::TypeRef type, bool ctor);
bool isSretReturn(widen::TypeRef t);
bool isAstLvalue(ast::Node const& n);
std::string emitLvalueAddr(ast::Node const& lv, SymTab const& syms,
                           strings::Pool& pool, std::ostream& out,
                           diagnostic::Sink& diag, bool allow_partial = false);
int cgAggSlotCount(widen::TypeRef t);
widen::TypeRef cgAggSlotType(widen::TypeRef t, int i);
std::string emitConvertWalk(std::string const& src_val,
                            widen::TypeRef src, widen::TypeRef dst,
                            std::ostream& out);
std::string emitImplicitAggregateConvert(std::string const& src_val,
                                          widen::TypeRef src, widen::TypeRef dst,
                                          int file_id, int tok,
                                          std::ostream& out,
                                          diagnostic::Sink& diag);

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
        case widen::Type::Form::kTuple:
        case widen::Type::Form::kSlid: {
            // A tuple or a class -> an LLVM literal struct over its slot/field
            // types: { llvm(t0), llvm(t1) }. A class IS a named tuple, so it
            // lowers identically (the name is a compile-time concept, erased
            // here); every aggregate path is shared.
            std::string s = "{ ";
            for (std::size_t i = 0; i < t.slots.size(); i++) {
                if (i) s += ", ";
                s += llvmForRef(t.slots[i]);
            }
            return s + " }";
        }
        case widen::Type::Form::kAlias:
            return llvmForRef(t.underlying);   // transparent — lower the underlying
        case widen::Type::Form::kConst:
            return llvmForRef(t.underlying);   // const is erased at the IR level
        case widen::Type::Form::kNone:
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
        widen::Type::Form of = widen::form(widen::strip(operand_type));
        if (isFloatType(operand_type)) {
            out << "  " << tmp << " = fcmp oeq " << llty << " "
                << v << ", 0.0\n";
        } else if (of == widen::Type::Form::kPointer
                || of == widen::Type::Form::kIterator
                || of == widen::Type::Form::kAnyptr) {
            // a pointer-like is false iff null; `icmp eq ptr v, 0` is invalid LLVM.
            out << "  " << tmp << " = icmp eq ptr " << v << ", null\n";
        } else {
            out << "  " << tmp << " = icmp eq " << llty << " "
                << v << ", 0\n";
        }
        return widen::convert(tmp, widen::intern("bool"), dest_type,
                              expr.file_id, expr.tok, out, diag);
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

// Element-wise arithmetic over aggregate VALUES (array and/or tuple), recursing
// for a nested aggregate slot/element and broadcasting a scalar operand. lv/rv are
// SSA registers of types lt/rt; resT is the result aggregate (tuple or array,
// decided by classify's aggregateArithType). extractvalue/insertvalue work on a
// `[N x T]` array exactly like a struct. Returns the result SSA register.
std::string emitAggregateArith(std::string const& op,
                               std::string const& lv, widen::TypeRef lt,
                               std::string const& rv, widen::TypeRef rt,
                               widen::TypeRef resT, int file, int tok,
                               std::ostream& out, diagnostic::Sink& diag) {
    auto isAgg = [](widen::TypeRef t) {
        widen::Type::Form f = widen::form(widen::strip(t));
        return f == widen::Type::Form::kTuple || f == widen::Type::Form::kArray;
    };
    // The per-slot type of an aggregate at index i: a tuple's slot, or an array's
    // element-with-remaining-dims (same for every i).
    auto slotType = [](widen::TypeRef t, std::size_t i) -> widen::TypeRef {
        widen::TypeRef s = widen::strip(t);
        if (widen::form(s) == widen::Type::Form::kTuple) return widen::get(s).slots[i];
        widen::Type const& a = widen::get(s);
        widen::TypeRef elem = a.elem;                    // copy before any intern
        std::vector<int> rest(a.dims.begin() + (a.dims.empty() ? 0 : 1),
                              a.dims.end());
        return rest.empty() ? elem : widen::internArray(elem, rest);
    };
    // Result slot types + count.
    widen::TypeRef rsT = widen::strip(resT);
    std::vector<widen::TypeRef> rslots;
    bool resTup = widen::form(rsT) == widen::Type::Form::kTuple;
    assert((resTup || widen::form(rsT) == widen::Type::Form::kArray)
        && "emitAggregateArith: result type is not an aggregate");
    if (resTup) {
        rslots = widen::get(rsT).slots;
    } else {
        widen::Type const& a = widen::get(rsT);
        int n = a.dims.empty() ? 0 : a.dims[0];
        widen::TypeRef elem = a.elem;                    // copy before any intern
        std::vector<int> rest(a.dims.begin() + (a.dims.empty() ? 0 : 1),
                              a.dims.end());
        widen::TypeRef slot = rest.empty() ? elem : widen::internArray(elem, rest);
        rslots.assign(static_cast<std::size_t>(n < 0 ? 0 : n), slot);
    }
    std::string aggty = llvmForRef(resT);
    std::string acc = "undef";
    for (std::size_t i = 0; i < rslots.size(); i++) {
        widen::TypeRef st = rslots[i];
        // Each operand: extract slot i (aggregate) or broadcast the scalar.
        std::string lslot = lv; widen::TypeRef lslotT = lt;
        if (isAgg(lt)) {
            std::string ex = newTmp("lx");
            out << "  " << ex << " = extractvalue " << llvmForRef(lt) << " "
                << lv << ", " << i << "\n";
            lslot = ex; lslotT = slotType(lt, i);
        }
        std::string rslot = rv; widen::TypeRef rslotT = rt;
        if (isAgg(rt)) {
            std::string ex = newTmp("rx");
            out << "  " << ex << " = extractvalue " << llvmForRef(rt) << " "
                << rv << ", " << i << "\n";
            rslot = ex; rslotT = slotType(rt, i);
        }
        std::string r;
        if (isAgg(st)) {
            r = emitAggregateArith(op, lslot, lslotT, rslot, rslotT, st,
                                   file, tok, out, diag);
        } else {
            std::string lc = widen::convert(lslot, lslotT, st, file, tok, out, diag);
            std::string rc = widen::convert(rslot, rslotT, st, file, tok, out, diag);
            r = emitArithInstr(op, lc, rc, st, out);
        }
        std::string tmp = newTmp(resTup ? "tup" : "arr");
        out << "  " << tmp << " = insertvalue " << aggty << " " << acc << ", "
            << llvmForRef(st) << " " << r << ", " << i << "\n";
        acc = tmp;
    }
    return acc;
}

// Scalar shift leaf — `lv << rv` / `lv >> rv` at the lhs type `lt`. A FLOAT lhs
// shifts as `lv * (1<<rv)` / `lv / (1<<rv)` (per fold.sl); an integer lhs width-
// matches the count to `lt` then shl / lshr (unsigned) / ashr (signed). Returns
// the result at type `lt` (no dest-type convert — the caller widens if needed).
std::string emitScalarShift(std::string const& op, std::string const& lv,
                            widen::TypeRef lt, std::string const& rv,
                            widen::TypeRef rt, std::ostream& out) {
    if (isFloatType(lt)) {
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
        return tmp;
    }
    widen::TypeKind lk, rk;
    widen::classify(lt, lk);
    widen::classify(rt, rk);
    std::string lllty = llvmForRef(lt);
    std::string rllty = llvmForRef(rt);
    std::string cnt = rv;
    if (rk.bits != lk.bits) {
        std::string tmp = newTmp("shft");
        if (rk.bits > lk.bits) {
            out << "  " << tmp << " = trunc " << rllty << " " << cnt
                << " to " << lllty << "\n";
        } else /* rk.bits < lk.bits — zext to wider */ {
            out << "  " << tmp << " = zext " << rllty << " " << cnt
                << " to " << lllty << "\n";
        }
        cnt = tmp;
    }
    bool uns = isUnsignedType(lt);
    std::string instr = (op == "<<") ? "shl" : (uns ? "lshr" : "ashr");
    std::string tmp = newTmp("bin");
    out << "  " << tmp << " = " << instr << " " << lllty
        << " " << lv << ", " << cnt << "\n";
    return tmp;
}

// Slot-wise aggregate shift (an array IS a homogeneous tuple): shift each lhs
// slot, recursing for a nested slot. A SCALAR count broadcasts (`rv` used at every
// slot); an AGGREGATE count applies per slot (extractvalue rt, i). The result IS
// the lhs aggregate type. Defined below (after cgAggSlotCount/cgAggSlotType).
std::string emitAggregateShift(std::string const& op, std::string const& lv,
                               widen::TypeRef lt, std::string const& rv,
                               widen::TypeRef rt, std::ostream& out);

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
        // Classify already validated the operands; nothing to recheck here.
        std::string lv = emitExpr(lhs, syms, pool, out, diag, lt);
        std::string rv = emitExpr(rhs, syms, pool, out, diag, rt);
        // Aggregate lhs: shift each slot (a scalar count broadcasts, an aggregate
        // count applies per slot). The result IS the lhs aggregate type, so no
        // dest-type convert (dest_type == lt here).
        widen::Type::Form lf = widen::form(widen::strip(lt));
        if (lf == widen::Type::Form::kTuple || lf == widen::Type::Form::kArray) {
            return emitAggregateShift(op, lv, lt, rv, rt, out);
        }
        std::string r = emitScalarShift(op, lv, lt, rv, rt, out);
        return widen::convert(r, lt, dest_type, expr.file_id, expr.tok, out, diag);
    }

    widen::TypeRef opty = expr.op_type;
    assert(opty != widen::kNoType && "emitBinary: BinaryExpr missing op_type");

    // Aggregate result (array and/or tuple): element-wise op via emitAggregateArith
    // (extractvalue/insertvalue, a scalar operand broadcasts, nested slots recurse).
    // dest_type == opty here, so the aggregate is returned directly.
    if (widen::form(widen::strip(opty)) == widen::Type::Form::kTuple
        || widen::form(widen::strip(opty)) == widen::Type::Form::kArray) {
        std::string lv = emitExpr(lhs, syms, pool, out, diag, lhs.inferred_type);
        std::string rv = emitExpr(rhs, syms, pool, out, diag, rhs.inferred_type);
        return emitAggregateArith(op, lv, lhs.inferred_type, rv, rhs.inferred_type,
                                  opty, expr.file_id, expr.tok, out, diag);
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
                     diagnostic::Sink& diag, std::string const& sret_dst = "") {
    assert(call.children.size() == call.param_types.size()
        && "emitCall: arity should have been verified by classify");
    // Passing a non-primitive VALUE to a by-pointer param is the convenience
    // syntax (`f(s)` == `f(^s)`, spec: plan-declarator.txt PASSING). Two arms:
    //   LVALUE arg (named local / index / deref) — pass its address directly,
    //     no copy. The function mutates the caller's data.
    //   RVALUE arg (a call return, op+ result, tuple literal) — materialize the
    //     value in a temp alloca and pass that alloca's address.
    // Excludes a pointer-like arg (an iterator demoting to a reference passes
    // its own pointer, not a fresh temp).
    auto isValueByRef = [&](ast::Node const& arg, widen::TypeRef dest) {
        if (widen::form(widen::strip(dest)) != widen::Type::Form::kPointer)
            return false;
        if (arg.inferred_type == widen::kNoType) return false;
        widen::Type::Form af = widen::form(widen::strip(arg.inferred_type));
        return af != widen::Type::Form::kPointer
            && af != widen::Type::Form::kIterator
            && af != widen::Type::Form::kAnyptr;
    };
    // The LVALUE pass-direct path requires arg's type to MATCH the param's
    // pointee exactly; an aggregate widening (`int8[N]` arg into `int[N]^`
    // param) needs a materialized + converted temp. Same predicate used by the
    // arg loop below.
    auto argMaterializes = [&](ast::Node const& arg, widen::TypeRef dest) {
        if (!isValueByRef(arg, dest)) return false;
        if (!isAstLvalue(arg)) return true;
        widen::TypeRef pointee = widen::get(widen::strip(dest)).pointee;
        return widen::deepStrip(arg.inferred_type) != widen::deepStrip(pointee);
    };
    // If any arg materializes a temp alloca, bracket the call in stacksave/
    // stackrestore so the slot is freed each time — a materializing call in a
    // loop reuses the stack region instead of leaking an alloca per iteration.
    // An LVALUE arg with matching types passes its existing address with no
    // fresh alloca, so it doesn't trigger the bracket.
    // A value-position sret call allocas a result temp inside the call; bracket it
    // (like a materializing arg) so a call in a loop reuses the stack slot.
    bool materializes = isSretReturn(call.return_type) && sret_dst.empty();
    for (size_t i = 0; i < call.children.size(); i++)
        if (argMaterializes(*call.children[i], call.param_types[i])) {
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
            bool types_match =
                widen::deepStrip(arg.inferred_type) == widen::deepStrip(pointee);
            if (isAstLvalue(arg) && types_match) {
                std::string addr = emitLvalueAddr(arg, syms, pool, out, diag);
                arg_vals.push_back({"ptr", addr});
                continue;
            }
            std::string pll = llvmForRef(pointee);
            std::string v;
            if (types_match) {
                v = emitExpr(arg, syms, pool, out, diag, pointee);
            } else {
                // Aggregate widen: load source as-is, walk per-leaf with
                // widen::convert into the pointee type, materialize the result.
                v = emitExpr(arg, syms, pool, out, diag, widen::kNoType);
                v = emitImplicitAggregateConvert(
                    v, arg.inferred_type, pointee, arg.file_id, arg.tok, out, diag);
            }
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
    if (isSretReturn(call.return_type)) {
        // sret: the callee CONSTRUCTS its result into a caller slot, passed as the
        // leading argument. Use the destination the caller gave (statement intercept)
        // or — in a value position — a fresh temp we load and return by value.
        // A HOOK-bearing return in a value position (an arg / operand, not the rhs of
        // a decl / assign / discarded call) would construct a temp the caller can't
        // destroy from here (no dtor scope) — it leaks / unbalances. Until the
        // desugar lift (inline use -> a temp decl) lands, reject it cleanly rather
        // than miscompile. Assign the call to a variable first.
        if (sret_dst.empty() && typeNeedsHook(call.return_type, /*ctor=*/true)) {
            diagnostic::report(diag, {call.file_id, call.tok,
                "Returning a class by value in an expression position is not yet "
                "supported — assign the call to a variable first.", {}});
        }
        std::string slot = sret_dst;
        if (slot.empty()) {
            slot = newTmp("srettmp");
            out << "  " << slot << " = alloca " << ret_llty << "\n";
        }
        out << "  call void @" << call.name << "(ptr " << slot;
        for (size_t i = 0; i < arg_vals.size(); i++)
            out << ", " << arg_vals[i].first << " " << arg_vals[i].second;
        out << ")\n";
        if (sret_dst.empty()) {
            result = newTmp("call");
            out << "  " << result << " = load " << ret_llty << ", ptr " << slot << "\n";
        }
        if (materializes)
            out << "  call void @llvm.stackrestore.p0(ptr " << sp << ")\n";
        return result;
    }
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
                            diagnostic::Sink& diag, bool allow_partial = false) {
    std::vector<ast::Node const*> chain;   // outermost .. innermost kIndexExpr
    ast::Node const* node = &index_expr;
    while (node->kind == ast::Kind::kIndexExpr) {
        chain.push_back(node);
        node = node->children[0].get();
    }
    // The base is an ADDRESS holding the object: a variable's alloca, or — for a
    // deref `ptr^...` — the pointer VALUE (the object lives at the pointee). The
    // index walk below dispatches on the CURRENT type each step — array dim, tuple/
    // field slot, or iterator (load the sequence pointer, GEP by element) — and
    // advances `cur`. This composes nested arrays (alias-element), arrays of
    // tuples, array-typed tuple slots, AND deref/iterator field access in ANY
    // order, and is where a class `op[]` plugs in. A genuine over-index is caught
    // in classify, so a non-indexable `cur` mid-chain is unreachable here.
    std::string addr;
    widen::TypeRef cur;
    if (node->kind == ast::Kind::kDerefExpr) {
        addr = emitExpr(*node->children[0], syms, pool, out, diag,
                        node->children[0]->inferred_type);
        cur = node->inferred_type;   // the pointee
    } else {
        assert(node->kind == ast::Kind::kIdentExpr && node->resolved_entry_id >= 0
            && "emitElementAddr: subscript base must be a variable or a deref");
        auto bit = syms.find(node->resolved_entry_id);
        assert(bit != syms.end() && "emitElementAddr: base not in SymTab");
        addr = bit->second.alloca_name;
        cur = bit->second.slids_type;
        // Implicit deref for the ARRAY-BY-POINTER param shorthand: `int a[3]`
        // as a parameter has resolved type `int[3]^` (the mungeParamType array
        // arm rewrites it), but the body indexes it WITHOUT an explicit `^`.
        // Load the pointer once at the base; the chain walks the array as usual.
        widen::TypeRef cs0 = widen::strip(cur);
        if (widen::form(cs0) == widen::Type::Form::kPointer) {
            widen::TypeRef pointee = widen::get(cs0).pointee;
            if (widen::form(widen::strip(pointee)) == widen::Type::Form::kArray) {
                std::string loaded = newTmp("aptr");
                out << "  " << loaded << " = load ptr, ptr " << addr << "\n";
                addr = loaded;
                cur = pointee;
            }
        }
    }
    // The FIRST subscript is chain.back(), the LAST is chain.front() (source order).
    for (std::size_t j = chain.size(); j-- > 0; ) {
        ast::Node const& idx_node = *chain[j]->children[1];
        widen::TypeRef cs = widen::strip(cur);
        widen::Type::Form f = widen::form(cs);
        if (f == widen::Type::Form::kIterator) {
            // `addr` holds the sequence pointer — load it, GEP by element type.
            std::string ptr = newTmp("itp");
            out << "  " << ptr << " = load ptr, ptr " << addr << "\n";
            std::string idx = emitExpr(idx_node, syms, pool, out, diag,
                                       widen::intern("int64"));
            widen::TypeRef pe = widen::get(cs).pointee;
            std::string gep = newTmp("elt");
            out << "  " << gep << " = getelementptr " << llvmForRef(pe) << ", ptr "
                << ptr << ", i64 " << idx << "\n";
            addr = gep;
            cur = pe;
        } else if (f == widen::Type::Form::kArray) {
            std::string idx = emitExpr(idx_node, syms, pool, out, diag,
                                       widen::intern("int64"));
            std::string gep = newTmp("elt");
            out << "  " << gep << " = getelementptr inbounds " << llvmForRef(cur)
                << ", ptr " << addr << ", i64 0, i64 " << idx << "\n";
            addr = gep;
            std::vector<int> const& dd = widen::get(cs).dims;
            cur = (dd.size() <= 1)
                ? widen::get(cs).elem
                : widen::internArray(widen::get(cs).elem,
                      std::vector<int>(dd.begin() + 1, dd.end()));
        } else if (f == widen::Type::Form::kTuple
                || f == widen::Type::Form::kSlid) {
            // A tuple slot or a class field (a class is a named tuple) -> a
            // struct GEP by the constant slot/field index.
            long k = std::strtol(idx_node.text.c_str(), nullptr, 10);
            std::vector<widen::TypeRef> const& slots = widen::get(cs).slots;
            assert(k >= 0 && k < static_cast<long>(slots.size())
                && "emitElementAddr: tuple/field slot index out of range");
            std::string gep = newTmp("slot");
            out << "  " << gep << " = getelementptr inbounds " << llvmForRef(cur)
                << ", ptr " << addr << ", i32 0, i32 " << k << "\n";
            addr = gep;
            cur = slots[k];
        } else {
            assert(false && "emitElementAddr: indexing a non-indexable type "
                            "(classify validates the rank)");
        }
    }
    // A PARTIAL index leaves `cur` an ARRAY (a sub-array slice) — valid only as a
    // store target / address (allow_partial); a value/read context has no scalar.
    // A tuple or scalar `cur` is a complete index (e.g. `a7[0]` -> a tuple value).
    if (widen::form(widen::strip(cur)) == widen::Type::Form::kArray && !allow_partial) {
        diagnostic::report(diag, {index_expr.file_id, index_expr.tok,
            "An array subscript must index every dimension.", {}});
        return "null";
    }
    return addr;
}

// Lockstep aggregate walk for `(Type=expr)` — convert `src_val` (typed `src`)
// into a value of type `dst`. A tuple/array target is built slot-by-slot /
// element-by-element from `undef`; each leaf bottoms out at convertExplicit
// (the scalar value grid). Mirrors classify's checkConvertCompat shape; classify
// has already validated arity/dim/leaf compatibility, so no error path here.
//
// ARENA SAFETY: widen::get returns a reference into the arena's reallocatable
// vector, so an intern* call anywhere downstream can dangle it. A recursive
// emitConvertWalk inside the loop CAN intern (the kArray multi-dim path mints
// an inner-array handle). Capture slots / dims / elem BY VALUE before the loop
// instead of holding `auto const&`.
std::string emitConvertWalk(std::string const& src_val,
                            widen::TypeRef src, widen::TypeRef dst,
                            std::ostream& out) {
    using F = widen::Type::Form;
    widen::TypeRef ds = widen::strip(dst);
    F df = widen::form(ds);
    // Aggregate: walk the destination's slots, extracting the matching source slot.
    // extractvalue's integer index is identical for an LLVM array `[N x T]` and a
    // struct, so the SAME walk converts array <-> tuple at any nesting (cross-form);
    // slot TYPES decompose form-agnostically on each side. (Mirrors
    // emitImplicitAggregateConvert; the leaf op differs — convertExplicit.)
    if (df == F::kTuple || df == F::kArray) {
        int n = cgAggSlotCount(ds);
        std::string src_ll = llvmForRef(src);
        std::string dst_ll = llvmForRef(dst);
        std::string result = "undef";
        for (int i = 0; i < n; i++) {
            widen::TypeRef dSlot = cgAggSlotType(ds, i);
            widen::TypeRef sSlot = cgAggSlotType(widen::strip(src), i);
            std::string slot_ll = llvmForRef(dSlot);
            std::string slot_src = newTmp("extt");
            out << "  " << slot_src << " = extractvalue " << src_ll
                << " " << src_val << ", " << i << "\n";
            std::string slot_conv = emitConvertWalk(slot_src, sSlot, dSlot, out);
            std::string next = newTmp("inst");
            out << "  " << next << " = insertvalue " << dst_ll
                << " " << result << ", " << slot_ll
                << " " << slot_conv << ", " << i << "\n";
            result = next;
        }
        return result;
    }
    // Leaf: scalar value grid. classify guarantees df is kPrimitive here (kVoid
    // / kPointer / kIterator / kSlid are rejected upstream); a regression hits
    // convertExplicit's "non-value destination" assert rather than emitting bad
    // IR silently.
    return widen::convertExplicit(src_val, src, dst, out);
}

// Lockstep aggregate walk for an IMPLICIT assignment with different element
// types — `int dst[N] = int8_src;`, `(int,int) tt = (int8,int8)_var`. Same
// shape as emitConvertWalk; leaves use widen::convert (the implicit widen,
// which REJECTS narrowing / cross-family / sign-change) instead of
// convertExplicit. Classify has pre-validated shape (dims/arity) at every
// composite level; leaf-level narrowing is the only error path here.
//
// ARENA SAFETY: same as emitConvertWalk — capture slots / dims / elem by value
// before the loop (the multi-dim path calls internArray; held `auto const&`
// from widen::get would dangle through the recursion).
// Form-agnostic aggregate decomposition (an array IS a homogeneous tuple): the
// number of top-level slots, and the i-th slot type. ARENA: capture elem / dims
// by value before internArray (a held widen::get ref would dangle on intern).
int cgAggSlotCount(widen::TypeRef t) {
    widen::TypeRef s = widen::strip(t);
    if (widen::form(s) == widen::Type::Form::kTuple) {
        return static_cast<int>(widen::get(s).slots.size());
    }
    std::vector<int> const& dims = widen::get(s).dims;
    return dims.empty() ? 0 : dims[0];
}
widen::TypeRef cgAggSlotType(widen::TypeRef t, int i) {
    widen::TypeRef s = widen::strip(t);
    if (widen::form(s) == widen::Type::Form::kTuple) {
        return widen::get(s).slots[i];
    }
    widen::Type const& a = widen::get(s);
    widen::TypeRef elem = a.elem;                       // copy before any intern
    std::vector<int> rest(a.dims.begin() + 1, a.dims.end());
    return rest.empty() ? elem : widen::internArray(elem, rest);
}

std::string emitAggregateShift(std::string const& op, std::string const& lv,
                               widen::TypeRef lt, std::string const& rv,
                               widen::TypeRef rt, std::ostream& out) {
    using F = widen::Type::Form;
    F rf = widen::form(widen::strip(rt));
    bool rAgg = (rf == F::kTuple || rf == F::kArray);   // aggregate count vs broadcast
    int n = cgAggSlotCount(lt);
    std::string lt_ll = llvmForRef(lt);
    std::string rt_ll = llvmForRef(rt);
    std::string acc = "undef";
    for (int i = 0; i < n; i++) {
        widen::TypeRef lSlot = cgAggSlotType(lt, i);    // capture before any intern
        std::string slot_ll = llvmForRef(lSlot);
        std::string lslot = newTmp("lsx");
        out << "  " << lslot << " = extractvalue " << lt_ll << " " << lv
            << ", " << i << "\n";
        // The count: extract slot i (aggregate count) or broadcast the scalar.
        std::string rslot = rv;
        widen::TypeRef rSlot = rt;
        if (rAgg) {
            rSlot = cgAggSlotType(rt, i);
            std::string ex = newTmp("rsx");
            out << "  " << ex << " = extractvalue " << rt_ll << " " << rv
                << ", " << i << "\n";
            rslot = ex;
        }
        F lsf = widen::form(widen::strip(lSlot));
        std::string r = (lsf == F::kTuple || lsf == F::kArray)
            ? emitAggregateShift(op, lslot, lSlot, rslot, rSlot, out)
            : emitScalarShift(op, lslot, lSlot, rslot, rSlot, out);
        std::string tmp = newTmp("shf");
        out << "  " << tmp << " = insertvalue " << lt_ll << " " << acc
            << ", " << slot_ll << " " << r << ", " << i << "\n";
        acc = tmp;
    }
    return acc;
}

std::string emitImplicitAggregateConvert(std::string const& src_val,
                                          widen::TypeRef src, widen::TypeRef dst,
                                          int file_id, int tok,
                                          std::ostream& out,
                                          diagnostic::Sink& diag) {
    using F = widen::Type::Form;
    widen::TypeRef ds = widen::strip(dst);
    F df = widen::form(ds);
    // Leaf: widen::convert (implicit widen — reports narrowing / cross-family).
    if (df != F::kTuple && df != F::kArray) {
        return widen::convert(src_val, src, dst, file_id, tok, out, diag);
    }
    // Aggregate: walk the destination's slots, extracting the matching source
    // slot (extractvalue's integer index is identical for an LLVM array `[N x T]`
    // and a struct `{...}`, so the SAME walk converts array <-> tuple at any
    // nesting). Slot TYPES decompose form-agnostically on each side.
    int n = cgAggSlotCount(ds);
    std::string src_ll = llvmForRef(src);
    std::string dst_ll = llvmForRef(dst);
    std::string result = "undef";
    for (int i = 0; i < n; i++) {
        widen::TypeRef dSlot = cgAggSlotType(ds, i);
        widen::TypeRef sSlot = cgAggSlotType(widen::strip(src), i);
        std::string slot_ll = llvmForRef(dSlot);
        std::string slot_src = newTmp("iaet");
        out << "  " << slot_src << " = extractvalue " << src_ll
            << " " << src_val << ", " << i << "\n";
        std::string slot_conv = emitImplicitAggregateConvert(
            slot_src, sSlot, dSlot, file_id, tok, out, diag);
        std::string next = newTmp("iait");
        out << "  " << next << " = insertvalue " << dst_ll
            << " " << result << ", " << slot_ll
            << " " << slot_conv << ", " << i << "\n";
        result = next;
    }
    return result;
}

// True if `ty` is, or (for a tuple) transitively contains, a pointer / iterator
// leaf — i.e. a move must null something inside it.
bool typeHasPointer(widen::TypeRef ty) {
    widen::TypeRef s = widen::strip(ty);
    widen::Type::Form f = widen::form(s);
    if (f == widen::Type::Form::kPointer || f == widen::Type::Form::kIterator) {
        return true;
    }
    // A class is a named tuple — recurse its fields the same way.
    if (f == widen::Type::Form::kTuple || f == widen::Type::Form::kSlid) {
        for (widen::TypeRef slot : widen::get(s).slots) {
            if (typeHasPointer(slot)) return true;
        }
        return false;
    }
    if (f == widen::Type::Form::kArray) return typeHasPointer(widen::get(s).elem);
    return false;
}

// An lvalue expression's address: a bare variable is its alloca; an index is
// emitElementAddr; a deref's address is the pointer value it loads.
std::string emitLvalueAddr(ast::Node const& lv, SymTab const& syms,
                           strings::Pool& pool, std::ostream& out,
                           diagnostic::Sink& diag, bool allow_partial) {
    if (lv.kind == ast::Kind::kIdentExpr) {
        auto it = syms.find(lv.resolved_entry_id);
        assert(it != syms.end() && "emitLvalueAddr: ident not in SymTab");
        return it->second.alloca_name;
    }
    if (lv.kind == ast::Kind::kIndexExpr) {
        // A PARTIAL array index (a sub-array slice) is a valid swap / move operand
        // — the whole-value load/store at the operand's type handles it. The caller
        // opts in (allow_partial); a scalar-context lvalue keeps the full-index rule.
        return emitElementAddr(lv, syms, pool, out, diag, allow_partial);
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
// `ty`): a pointer / iterator leaf gets `store ptr null`; a tuple or array
// recurses into each slot / element that transitively holds a pointer; a
// primitive is left untouched. The address-level GEP recursion handles
// arbitrarily nested tuples and arrays (the move "fancy case", whole-array move).
void emitNullLeaves(std::string const& addr, widen::TypeRef ty,
                    std::ostream& out) {
    widen::TypeRef s = widen::strip(ty);
    widen::Type::Form f = widen::form(s);
    if (f == widen::Type::Form::kPointer || f == widen::Type::Form::kIterator) {
        out << "  store ptr null, ptr " << addr << "\n";
        return;
    }
    // A class is a named tuple: null its fields' pointer leaves the same way.
    if (f == widen::Type::Form::kTuple || f == widen::Type::Form::kSlid) {
        std::string tll = llvmForRef(s);
        std::vector<widen::TypeRef> const& slots = widen::get(s).slots;
        for (std::size_t i = 0; i < slots.size(); i++) {
            if (!typeHasPointer(slots[i])) continue;
            std::string gep = newTmp("nleaf");
            out << "  " << gep << " = getelementptr inbounds " << tll
                << ", ptr " << addr << ", i32 0, i32 " << i << "\n";
            emitNullLeaves(gep, slots[i], out);
        }
        return;
    }
    if (f == widen::Type::Form::kArray) {
        // Null each element that has pointer leaves: flat-walk the (row-major
        // contiguous) array and recurse per element. A pointer-free element array
        // (`int[3]`) has nothing to null and is skipped.
        widen::TypeRef elem = widen::get(s).elem;
        if (!typeHasPointer(elem)) return;
        std::string elem_ll = llvmForRef(elem);
        long total = 1;
        for (int d : widen::get(s).dims) total *= d;
        for (long i = 0; i < total; i++) {
            std::string gep = newTmp("nleaf");
            out << "  " << gep << " = getelementptr " << elem_ll
                << ", ptr " << addr << ", i64 " << i << "\n";
            emitNullLeaves(gep, elem, out);
        }
        return;
    }
    // Any other form is a leaf with nothing to null: a primitive (the common case).
}

// `*addr ±= 1` for ONE scalar / iterator leaf at `addr`, typed `leaf`. An iterator
// steps by one ELEMENT (load ptr, GEP ±1, store); a numeric leaf loads, add/sub 1
// (float uses fadd/fsub + 1.0), and stores. Returns the new SSA value (callers that
// discard it — every PPID bump does — ignore it). This is the per-leaf bump shared
// by the scalar and the aggregate (leaf-walk) inc/dec.
std::string emitLeafBump(std::string const& addr, widen::TypeRef leaf,
                         std::string const& op, std::ostream& out) {
    if (isIteratorType(leaf)) {
        std::string cur = newTmp("itld");
        out << "  " << cur << " = load ptr, ptr " << addr << "\n";
        std::string elem_ll = llvmForRef(pointeeTypeC(leaf));
        std::string nv = newTmp("itinc");
        out << "  " << nv << " = getelementptr " << elem_ll << ", ptr "
            << cur << ", i64 " << (op == "++" ? "1" : "-1") << "\n";
        out << "  store ptr " << nv << ", ptr " << addr << "\n";
        return nv;
    }
    std::string llty = llvmForRef(leaf);
    bool flt = isFloatType(leaf);
    std::string cur = newTmp("ld");
    out << "  " << cur << " = load " << llty << ", ptr " << addr << "\n";
    std::string nv = newTmp("inc");
    char const* instr = flt ? (op == "++" ? "fadd" : "fsub")
                            : (op == "++" ? "add" : "sub");
    char const* one = flt ? "1.0" : "1";
    out << "  " << nv << " = " << instr << " " << llty << " " << cur << ", "
        << one << "\n";
    out << "  store " << llty << " " << nv << ", ptr " << addr << "\n";
    return nv;
}

// `*addr ±= 1` over an aggregate (tuple / array) at `addr`: step EVERY leaf. A
// tuple GEPs each slot (`i32 0, i32 i`) and recurses per slot type; an array
// flat-walks its contiguous row-major layout and recurses on the element; a
// scalar / iterator leaf bumps via emitLeafBump. Mirrors emitNullLeaves' walk.
void emitAggBump(std::string const& addr, widen::TypeRef ty,
                 std::string const& op, std::ostream& out) {
    widen::TypeRef s = widen::strip(ty);
    widen::Type::Form f = widen::form(s);
    if (f == widen::Type::Form::kTuple) {
        std::string tll = llvmForRef(s);
        std::vector<widen::TypeRef> slots = widen::get(s).slots;   // copy: no intern
        for (std::size_t i = 0; i < slots.size(); i++) {
            std::string gep = newTmp("bleaf");
            out << "  " << gep << " = getelementptr inbounds " << tll
                << ", ptr " << addr << ", i32 0, i32 " << i << "\n";
            if (widen::form(widen::strip(slots[i])) == widen::Type::Form::kTuple
                || widen::form(widen::strip(slots[i])) == widen::Type::Form::kArray)
                emitAggBump(gep, slots[i], op, out);
            else
                emitLeafBump(gep, slots[i], op, out);
        }
        return;
    }
    // Array: flat-walk all dims (row-major contiguous), recurse on the element.
    widen::TypeRef elem = widen::get(s).elem;            // copy: no intern
    std::string elem_ll = llvmForRef(elem);
    long total = 1;
    for (int d : widen::get(s).dims) total *= d;
    bool elemAgg = widen::form(widen::strip(elem)) == widen::Type::Form::kTuple
                || widen::form(widen::strip(elem)) == widen::Type::Form::kArray;
    for (long i = 0; i < total; i++) {
        std::string gep = newTmp("bleaf");
        out << "  " << gep << " = getelementptr " << elem_ll << ", ptr "
            << addr << ", i64 " << i << "\n";
        if (elemAgg) emitAggBump(gep, elem, op, out);
        else         emitLeafBump(gep, elem, op, out);
    }
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
                // `^arr[i]` — the element GEP. A PARTIAL index yields a reference to
                // a SUB-ARRAY slice (`^a3[i]` -> int[2]^), used by a multi-dim
                // for-loop's by-ref binding; allow it.
                return emitElementAddr(operand, syms, pool, out, diag,
                                       /*allow_partial=*/true);
            }
            assert(operand.kind == ast::Kind::kIdentExpr
                && operand.resolved_entry_id >= 0
                && "kAddrOfExpr: operand must be a resolved variable");
            auto it = syms.find(operand.resolved_entry_id);
            assert(it != syms.end() && "kAddrOfExpr: operand not in SymTab");
            return it->second.alloca_name;
        }
        case ast::Kind::kIndexExpr: {
            // A tuple / class-field slot read `tup[k]` (a class is a named tuple,
            // so `base.field` lowers here): emit the aggregate value, extract slot
            // k (classify guaranteed k is a constant in range).
            ast::Node const& ibase = *expr.children[0];
            widen::Type::Form ibf = widen::form(widen::strip(ibase.inferred_type));
            if (ibf == widen::Type::Form::kTuple || ibf == widen::Type::Form::kSlid) {
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
            // `arr[i]` rvalue: address the element, load it. A PARTIAL index yields
            // a SUB-ARRAY value (allow_partial) — the load reads the whole `[N x T]`
            // slice; classify rejects assigning it to a non-matching type.
            std::string addr = emitElementAddr(expr, syms, pool, out, diag,
                                               /*allow_partial=*/true);
            std::string llty = llvmForRef(expr.inferred_type);
            std::string tmp = newTmp("idx");
            out << "  " << tmp << " = load " << llty << ", ptr " << addr << "\n";
            return widen::convert(tmp, expr.inferred_type, dest_type,
                                  expr.file_id, expr.tok, out, diag);
        }
        case ast::Kind::kTupleExpr: {
            // A tuple LITERAL whose TYPE is an array (array-from-tuple, e.g.
            // `return (1,2,3)` for an int[3]) builds an `[N x T]` value, not a tuple
            // struct. The statement arms intercept this via emitArrayFromTuple, but
            // an rvalue position (a return / operand) reaches emitExpr directly — an
            // array has no `.slots`, so falling through would index an empty vector.
            if (widen::form(widen::strip(expr.inferred_type))
                    == widen::Type::Form::kArray) {
                return emitArrayLiteralValue(expr.inferred_type, expr,
                                             syms, pool, out, diag);
            }
            // Build the literal struct `{ t0, t1, ... }` by inserting each slot
            // value into an undef aggregate (value semantics).
            std::string llty = llvmForRef(expr.inferred_type);
            std::vector<widen::TypeRef> slots =
                widen::get(widen::strip(expr.inferred_type)).slots;
            std::string acc = "undef";
            for (std::size_t i = 0; i < expr.children.size(); i++) {
                // An ARRAY-typed slot built from a tuple literal is an array value
                // (`[N x T]`), not a nested tuple — build it element-wise.
                std::string v;
                if (widen::form(widen::strip(slots[i])) == widen::Type::Form::kArray
                    && expr.children[i]->kind == ast::Kind::kTupleExpr) {
                    v = emitArrayLiteralValue(slots[i], *expr.children[i],
                                              syms, pool, out, diag);
                } else {
                    v = emitExpr(*expr.children[i], syms, pool, out, diag, slots[i]);
                }
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
            // `ptr` (the result is T^ / T[]). A single CLASS object is constructed
            // in place (field-init + ctor hook); its size is the runtime __$sizeof().
            widen::TypeRef es = widen::strip(expr.return_type);
            bool is_class = widen::form(es) == widen::Type::Form::kSlid;
            std::string p;
            if (expr.children[1]) {            // placement: result is the address
                ast::Node const& addr = *expr.children[1];
                p = emitExpr(addr, syms, pool, out, diag, addr.inferred_type);
            } else {
                // Element byte size: a class is sized at runtime by its __$sizeof()
                // helper; everything else has a compile-time size.
                std::string elem_size;
                if (is_class) {
                    std::string sz = newTmp("esz");
                    out << "  " << sz << " = call i64 @" << widen::classSymbol(es)
                        << "__$sizeof()\n";
                    elem_size = sz;
                } else {
                    long long elem = widen::typeByteSize(expr.return_type);
                    // classify rejected an unsized element ("Cannot allocate") and
                    // main short-circuits, so a -1 here means a missed gate.
                    assert(elem >= 0 && "kNewExpr: unsized element reached codegen");
                    elem_size = std::to_string(elem);
                }
                if (expr.children[0]) {        // array: n * elem-size
                    std::string n = emitExpr(*expr.children[0], syms, pool, out,
                                             diag, widen::intern("int64"));
                    if (is_class) {
                        // A class array: EVERY element is field-initialized (the
                        // default value laid into the slot). A HOOK class additionally
                        // prepends an 8-byte count COOKIE (so delete can loop the dtor)
                        // and runs the ctor per element. The cookie/hook are gated on
                        // needs; the field-init is not (a trivial class still has
                        // field defaults).
                        bool needs = typeNeedsHook(es, /*ctor=*/false);
                        std::string db = newTmp("nbytes");
                        out << "  " << db << " = mul i64 " << n << ", "
                            << elem_size << "\n";
                        if (needs) {
                            std::string tot = newTmp("ntot");
                            out << "  " << tot << " = add i64 8, " << db << "\n";
                            std::string mptr = newTmp("newarr");
                            out << "  " << mptr << " = call ptr @malloc(i64 " << tot
                                << ")\n";
                            out << "  store i64 " << n << ", ptr " << mptr << "\n";
                            p = newTmp("newdata");
                            out << "  " << p << " = getelementptr i8, ptr " << mptr
                                << ", i64 8\n";
                        } else {
                            p = newTmp("new");
                            out << "  " << p << " = call ptr @malloc(i64 " << db
                                << ")\n";
                        }
                        std::string defv = emitExpr(*expr.children[2], syms, pool,
                                                    out, diag, expr.return_type);
                        std::string ll = llvmForRef(expr.return_type);
                        std::string pre = newLabel("newc_pre");
                        std::string cnd = newLabel("newc_cond");
                        std::string bdy = newLabel("newc_body");
                        std::string fin = newLabel("newc_end");
                        out << "  br label %" << pre << "\n";
                        out << pre << ":\n";
                        out << "  br label %" << cnd << "\n";
                        out << cnd << ":\n";
                        std::string i = newTmp("ci");
                        std::string inx = newTmp("cinext");
                        out << "  " << i << " = phi i64 [ 0, %" << pre << " ], [ "
                            << inx << ", %" << bdy << " ]\n";
                        std::string cmp = newTmp("ccmp");
                        out << "  " << cmp << " = icmp ult i64 " << i << ", " << n
                            << "\n";
                        out << "  br i1 " << cmp << ", label %" << bdy
                            << ", label %" << fin << "\n";
                        out << bdy << ":\n";
                        std::string elem = newTmp("celem");
                        out << "  " << elem << " = getelementptr " << ll << ", ptr "
                            << p << ", i64 " << i << "\n";
                        out << "  store " << ll << " " << defv << ", ptr " << elem
                            << "\n";
                        if (needs) emitConstructHooks(elem, expr.return_type, out);
                        out << "  " << inx << " = add i64 " << i << ", 1\n";
                        out << "  br label %" << cnd << "\n";
                        out << fin << ":\n";
                    } else {
                        std::string mul = newTmp("nbytes");
                        out << "  " << mul << " = mul i64 " << n << ", "
                            << elem_size << "\n";
                        p = newTmp("new");
                        out << "  " << p << " = call ptr @malloc(i64 " << mul
                            << ")\n";
                    }
                } else {
                    p = newTmp("new");
                    out << "  " << p << " = call ptr @malloc(i64 " << elem_size
                        << ")\n";
                }
            }
            // A single class object is constructed in place: field-init from the
            // construction tuple (children[2]), then run the ctor hook. (The array
            // form's per-element construction lands with the new[] cookie work.)
            if (is_class && !expr.children[0]
                && expr.children.size() > 2 && expr.children[2]) {
                std::string val = emitExpr(*expr.children[2], syms, pool, out,
                                           diag, expr.return_type);
                out << "  store " << llvmForRef(expr.return_type) << " " << val
                    << ", ptr " << p << "\n";
                emitConstructHooks(p, expr.return_type, out);
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
            // `(Type=operand)` value conversion. Scalar target: emit the operand
            // and apply the explicit grid (trunc/ext/fp<->int/sign reinterpret
            // /nonzero test). Tuple/array target: walk the aggregate in lockstep
            // — at each slot/element pair, extractvalue from the source, recurse
            // (so a tuple slot may itself be a tuple/array), insertvalue into
            // the result. Leaves bottom out at convertExplicit.
            assert(expr.children.size() == 1 && "kConvertExpr needs 1 operand");
            ast::Node const& operand = *expr.children[0];
            std::string v = emitExpr(operand, syms, pool, out, diag,
                                     operand.inferred_type);
            v = emitConvertWalk(v, operand.inferred_type, expr.inferred_type, out);
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
            // (widened into dest_type); the rest run for effect — bumps, or a
            // PPID address-once binding (`_$lv = ^leaf`) carried as a
            // kVarDeclStmt so a complex post-inc captures its leaf address at the
            // read point, inside this (possibly short-circuited) subtree.
            assert(expr.value_index >= 0
                && expr.value_index < static_cast<int>(expr.children.size())
                && "kSeqExpr: value_index out of range");
            std::string result;
            for (size_t i = 0; i < expr.children.size(); i++) {
                ast::Node const& ch = *expr.children[i];
                if (static_cast<int>(i) == expr.value_index) {
                    result = emitExpr(ch, syms, pool, out, diag, dest_type);
                } else if (ch.kind == ast::Kind::kVarDeclStmt) {
                    // `_$lv` is always a plain reference: its alloca was hoisted
                    // by collectVarDecls, so emit only the address store here.
                    auto it = syms.find(ch.resolved_entry_id);
                    assert(it != syms.end() && "kSeqExpr: _$lv alloca not hoisted");
                    std::string a = emitExpr(*ch.children[0], syms, pool, out, diag,
                                             it->second.slids_type);
                    out << "  store " << it->second.llvm_type << " " << a
                        << ", ptr " << it->second.alloca_name << "\n";
                } else {
                    emitExpr(ch, syms, pool, out, diag, widen::kNoType);
                }
            }
            return result;
        }
        case ast::Kind::kBumpExpr: {
            // `lvalue = lvalue ± 1`; the returned register (the new value) is
            // discarded by the enclosing seq. Two forms share the int/float/
            // iterator logic below, differing only in the leaf's ADDRESS and TYPE:
            //   scalar  — resolved_entry_id names a variable; addr = its alloca.
            //   complex — children[0] (the `_$lv` reference) loads to the leaf
            //             address; the leaf type rides on inferred_type.
            std::string addr;
            widen::TypeRef leaf;
            if (!expr.children.empty()) {
                addr = emitExpr(*expr.children[0], syms, pool, out, diag,
                                widen::kNoType);
                leaf = expr.inferred_type;
            } else {
                assert(expr.resolved_entry_id >= 0 && "kBumpExpr: missing entry");
                auto it = syms.find(expr.resolved_entry_id);
                assert(it != syms.end() && "kBumpExpr: entry not in SymTab");
                addr = it->second.alloca_name;
                leaf = it->second.slids_type;
            }
            // A tuple / array steps EVERY leaf (numeric ±1, iterator one element),
            // recursively — the per-leaf bump is the same as a scalar's. Its value
            // is discarded (the read node carries the phrase's value), so return "".
            widen::Type::Form lf = widen::form(widen::strip(leaf));
            if (lf == widen::Type::Form::kTuple || lf == widen::Type::Form::kArray) {
                emitAggBump(addr, leaf, expr.text, out);
                return "";
            }
            return emitLeafBump(addr, leaf, expr.text, out);
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
        case ast::Kind::kDtorCallStmt:
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
struct DtorScope;   // fwd

struct LoopCtx {
    std::string header_label;
    std::string exit_label;
    LoopCtx const* outer = nullptr;   // enclosing loop/switch — a labeled/numbered
                                      // break/continue walks `loop_levels` hops out
    DtorScope const* scope = nullptr; // the scope active OUTSIDE this loop — a
                                      // break/continue destroys every scope it
                                      // unwinds down to (but not including) this one
};

// A live destructible: the address of a class instance + the class name (its
// dtor symbol is `<class>__$dtor`). Tracked per lexical scope, destroyed in
// REVERSE declaration order on every exit (the destructor-balance invariant).
struct DtorObj { std::string addr; widen::TypeRef type; };
struct DtorScope {
    std::vector<DtorObj> objs;
    DtorScope const* outer = nullptr;
};

// Does this type run ctor (or dtor) hooks — a hook-bearing class, or an array /
// tuple CONTAINING one (recursive)?
bool typeNeedsHook(widen::TypeRef t, bool ctor) {
    using F = widen::Type::Form;
    widen::TypeRef s = widen::strip(t);
    F f = widen::form(s);
    if (f == F::kSlid)  return ctor ? widen::get(s).needs_ctor : widen::get(s).needs_dtor;
    if (f == F::kArray) return typeNeedsHook(widen::get(s).elem, ctor);
    if (f == F::kTuple) {
        for (widen::TypeRef slot : widen::get(s).slots)
            if (typeNeedsHook(slot, ctor)) return true;
    }
    return false;
}

// A non-primitive return value is passed via a caller-provided slot (sret): the
// function returns void and CONSTRUCTS the result into `%sret.in`. This is ALWAYS
// used for an array / tuple / class return. A HOOK-bearing return additionally
// needs the caller-side temp + default-move + dtor dance (typeNeedsHook gates that
// at the statement intercepts); a POD aggregate just flows through the value path
// (alloca a temp, call into it, load) — behavior-neutral vs the old by-value ABI.
bool isSretReturn(widen::TypeRef t) {
    using F = widen::Type::Form;
    F f = widen::form(widen::strip(t));
    return f == F::kArray || f == F::kTuple || f == F::kSlid;
}

// The element type of an array with the OUTERMOST dim stripped (the per-element
// type the hook walk recurses on).
widen::TypeRef arrayElemOf(widen::TypeRef arr) {
    widen::Type const& a = widen::get(widen::strip(arr));
    if (a.dims.size() <= 1) return a.elem;
    return widen::internArray(a.elem,
                              std::vector<int>(a.dims.begin() + 1, a.dims.end()));
}

// Itanium recursive descent. Construct: build each contained class (its hooks, in
// declaration / element order), THEN run a class's own ctor hook. Memory is
// already laid down (field-init); this runs the HOOKS. Descends class fields,
// tuple slots, AND array elements.
void emitConstructHooks(std::string const& addr, widen::TypeRef type,
                        std::ostream& out) {
    widen::TypeRef cs = widen::strip(type);
    widen::Type const& ct = widen::get(cs);
    std::string llty = llvmForRef(cs);
    if (ct.form == widen::Type::Form::kArray) {
        int n = ct.dims[0];   // capture before arrayElemOf interns (dangles ct)
        widen::TypeRef et = arrayElemOf(cs);
        if (typeNeedsHook(et, true)) {
            for (int i = 0; i < n; i++) {
                std::string gep = newTmp("ctorelt");
                out << "  " << gep << " = getelementptr inbounds " << llty
                    << ", ptr " << addr << ", i64 0, i64 " << i << "\n";
                emitConstructHooks(gep, et, out);
            }
        }
        return;
    }
    for (std::size_t i = 0; i < ct.slots.size(); i++) {     // kSlid / kTuple slots
        if (typeNeedsHook(ct.slots[i], true)) {
            std::string gep = newTmp("ctorfld");
            out << "  " << gep << " = getelementptr inbounds " << llty
                << ", ptr " << addr << ", i32 0, i32 " << i << "\n";
            emitConstructHooks(gep, ct.slots[i], out);
        }
    }
    if (ct.form == widen::Type::Form::kSlid && ct.has_ctor)
        out << "  call void @" << widen::classSymbol(cs) << "__$ctor(ptr " << addr << ")\n";
}

// Destruct: a class's own dtor hook FIRST, then tear down contained classes in
// REVERSE order — the mirror of construction (fields, slots, AND array elements).
void emitDestructHooks(std::string const& addr, widen::TypeRef type,
                       std::ostream& out) {
    widen::TypeRef cs = widen::strip(type);
    widen::Type const& ct = widen::get(cs);
    std::string llty = llvmForRef(cs);
    if (ct.form == widen::Type::Form::kArray) {
        int n = ct.dims[0];   // capture before arrayElemOf interns (dangles ct)
        widen::TypeRef et = arrayElemOf(cs);
        if (typeNeedsHook(et, false)) {
            for (int i = n; i-- > 0; ) {
                std::string gep = newTmp("dtorelt");
                out << "  " << gep << " = getelementptr inbounds " << llty
                    << ", ptr " << addr << ", i64 0, i64 " << i << "\n";
                emitDestructHooks(gep, et, out);
            }
        }
        return;
    }
    if (ct.form == widen::Type::Form::kSlid && ct.has_dtor)
        out << "  call void @" << widen::classSymbol(cs) << "__$dtor(ptr " << addr << ")\n";
    for (std::size_t i = ct.slots.size(); i-- > 0; ) {      // kSlid / kTuple slots
        if (typeNeedsHook(ct.slots[i], false)) {
            std::string gep = newTmp("dtorfld");
            out << "  " << gep << " = getelementptr inbounds " << llty
                << ", ptr " << addr << ", i32 0, i32 " << i << "\n";
            emitDestructHooks(gep, ct.slots[i], out);
        }
    }
}

// Emit the destructors for ONE scope, in reverse declaration order.
void emitScopeDtors(DtorScope const& s, std::ostream& out) {
    for (auto it = s.objs.rbegin(); it != s.objs.rend(); ++it) {
        emitDestructHooks(it->addr, it->type, out);
    }
}

// Emit dtors for every scope from `from` up to but NOT including `stop`
// (innermost first). `stop == nullptr` unwinds the whole chain (a return).
void emitUnwindDtors(DtorScope const* from, DtorScope const* stop,
                     std::ostream& out) {
    for (DtorScope const* s = from; s && s != stop; s = s->outer) {
        emitScopeDtors(*s, out);
    }
}

// Collect an array's ELEMENT value-nodes from a tuple literal, descending exactly
// dims.size() levels (the ARRAY dims). At that depth each node is one element,
// taken WHOLE — a scalar, or a tuple for a tuple element (NOT flattened). Mirrors
// classify's collectArrayElementNodes (classify validated the shape).
void collectArrayElementNodesAst(ast::Node const& n, std::vector<int> const& dims,
                                 std::size_t i, std::vector<ast::Node const*>& out) {
    if (i == dims.size()) { out.push_back(&n); return; }
    for (auto const& c : n.children) if (c)
        collectArrayElementNodesAst(*c, dims, i + 1, out);
}

// Build an `[N x T]` aggregate VALUE from a tuple literal — for an array-typed
// TUPLE SLOT, which needs the array as an SSA value (not stored into an alloca).
// Element-aware like emitArrayFromTuple; the flat index decomposes into the dim
// indices so a multi-dim slot inserts at the right nested position.
std::string emitArrayLiteralValue(widen::TypeRef arrType, ast::Node const& rhs,
                                  SymTab const& syms, strings::Pool& pool,
                                  std::ostream& out, diagnostic::Sink& diag) {
    widen::Type const& at = widen::get(widen::strip(arrType));
    widen::TypeRef elem = at.elem;
    std::vector<int> dims = at.dims;
    std::string arr_ll = llvmForRef(arrType);
    std::string elem_ll = llvmForRef(elem);
    std::vector<ast::Node const*> elems;
    collectArrayElementNodesAst(rhs, dims, 0, elems);
    long total = 1;
    for (int d : dims) total *= d;
    assert(static_cast<long>(elems.size()) == total
        && "emitArrayLiteralValue: element count != array size (classify validated)");
    bool elem_array = widen::form(widen::strip(elem)) == widen::Type::Form::kArray;
    std::string acc = "undef";
    for (std::size_t i = 0; i < elems.size(); i++) {
        // An ARRAY-typed element (a nested array) is built as an array value, not a
        // tuple — recurse; otherwise the leaf scalar / tuple value.
        std::string v = (elem_array && elems[i]->kind == ast::Kind::kTupleExpr)
            ? emitArrayLiteralValue(elem, *elems[i], syms, pool, out, diag)
            : emitExpr(*elems[i], syms, pool, out, diag, elem);
        std::vector<long> ix(dims.size());        // flat index -> dim indices
        long rem = static_cast<long>(i);
        for (int k = static_cast<int>(dims.size()) - 1; k >= 0; k--) {
            ix[k] = rem % dims[k];
            rem /= dims[k];
        }
        std::string tmp = newTmp("arr");
        out << "  " << tmp << " = insertvalue " << arr_ll << " " << acc
            << ", " << elem_ll << " " << v;
        for (long x : ix) out << ", " << x;
        out << "\n";
        acc = tmp;
    }
    return acc;
}

// Initialize / assign an array from a tuple LITERAL, element-wise. Each dims-deep
// node is emitted in the ELEMENT type's context (a scalar flexes/widens; a tuple
// element builds its aggregate) and stored at flat offset i — the array's element
// type drives the GEP stride, so a tuple element stays a `{...}` aggregate.
void emitArrayFromTuple(std::string const& alloca_name, widen::TypeRef arrType,
                        ast::Node const& rhs, SymTab const& syms,
                        strings::Pool& pool, std::ostream& out,
                        diagnostic::Sink& diag) {
    widen::Type const& at = widen::get(widen::strip(arrType));
    widen::TypeRef elem = at.elem;
    std::string elem_ll = llvmForRef(elem);
    bool elem_array = widen::form(widen::strip(elem)) == widen::Type::Form::kArray;
    std::vector<ast::Node const*> elems;
    collectArrayElementNodesAst(rhs, at.dims, 0, elems);
    for (std::size_t i = 0; i < elems.size(); i++) {
        // A nested ARRAY element is built as an array value (not a tuple).
        std::string v = (elem_array && elems[i]->kind == ast::Kind::kTupleExpr)
            ? emitArrayLiteralValue(elem, *elems[i], syms, pool, out, diag)
            : emitExpr(*elems[i], syms, pool, out, diag, elem);
        std::string gep = newTmp("aelt");
        out << "  " << gep << " = getelementptr " << elem_ll << ", ptr "
            << alloca_name << ", i64 " << i << "\n";
        out << "  store " << elem_ll << " " << v << ", ptr " << gep << "\n";
    }
}

// CROSS-FORM aggregate VALUE copies (array <-> tuple, any nesting) are lowered by
// slot in desugar (lowerAggregateList) into per-leaf stores, so no flatten helper
// is needed here. A copy whose source/dest differ only at the LEAVES (same shape,
// element widening) — and the seams desugar does not visit (return, call-arg,
// const decl) — go through emitImplicitAggregateConvert, which is itself
// form-agnostic (array == homogeneous tuple).

// The TypeRef of a tuple/array's i-th destructure element (a tuple slot, or the
// array's (sub-)element type). agg_type is guaranteed a tuple or array by classify.
widen::TypeRef destructureElem(widen::TypeRef agg_type, std::size_t i) {
    widen::TypeRef s = widen::strip(agg_type);
    if (widen::form(s) == widen::Type::Form::kTuple) return widen::get(s).slots[i];
    widen::Type const& at = widen::get(s);   // array
    return (at.dims.size() <= 1) ? at.elem
        : widen::internArray(at.elem,
              std::vector<int>(at.dims.begin() + 1, at.dims.end()));
}

// Bind a destructure's slots from the aggregate value `agg` (type agg_type): extract
// each element and store it to the slot's alloca; a NESTED slot extracts the
// sub-aggregate and recurses; a null slot is discarded. The rhs was evaluated once
// by the caller.
void emitDestructure(ast::Node const& node, std::string const& agg,
                     widen::TypeRef agg_type, SymTab& syms, std::ostream& out) {
    std::string llty = llvmForRef(agg_type);
    for (std::size_t i = 1; i < node.children.size(); i++) {
        ast::Node const* tgt = node.children[i].get();
        if (!tgt) continue;   // discard
        std::string slot = newTmp("dslot");
        out << "  " << slot << " = extractvalue " << llty << " " << agg
            << ", " << (i - 1) << "\n";
        if (tgt->kind == ast::Kind::kDestructureStmt) {
            emitDestructure(*tgt, slot, destructureElem(agg_type, i - 1), syms, out);
        } else {
            auto it = syms.find(tgt->resolved_entry_id);
            assert(it != syms.end() && "kDestructureStmt: target not in SymTab");
            out << "  store " << it->second.llvm_type << " " << slot
                << ", ptr " << it->second.alloca_name << "\n";
        }
    }
}

void emitStmt(ast::Node const& stmt, SymTab& syms,
              strings::Pool& pool,
              widen::TypeRef fn_return_type,
              LoopCtx const* loop,
              DtorScope* scope,
              std::ostream& out, diagnostic::Sink& diag) {
    // Stop at the first error: once any error is recorded, emit no further
    // statements. Codegen must not keep producing IR past a semantic failure —
    // and reporting one clear error beats a cascade of follow-ons.
    if (diagnostic::hasErrors(diag)) return;
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
            // sret CALL initializer (`Class x = fn()`). Phase B case 1 — BUILD IN
            // PLACE: a new decl's ctor hasn't run, so when the types match exactly
            // the callee constructs its result DIRECTLY into this local (no temp, no
            // move). A hook local is then registered for destruction at scope exit. A
            // non-exact (cross-form / leaf-widen) init has no exact slot to build
            // into; it falls through to the generic convert path below (POD), and a
            // hook leaf-widen call init doesn't arise (classify keeps a class init
            // same-type).
            if (!stmt.children.empty()
                && stmt.children[0]->kind == ast::Kind::kCallExpr
                && isSretReturn(it->second.slids_type)
                && widen::deepStrip(it->second.slids_type)
                       == widen::deepStrip(stmt.children[0]->return_type)) {
                widen::TypeRef T = it->second.slids_type;
                emitCall(*stmt.children[0], syms, pool, out, diag,
                         it->second.alloca_name);
                if (typeNeedsHook(T, /*ctor=*/true)) {
                    assert(scope && "sret call init without a dtor scope");
                    scope->objs.push_back({it->second.alloca_name, widen::strip(T)});
                }
                return;
            }
            // FIELD-INIT. Note this is an if/else CHAIN (no early return) so the
            // lifecycle-hook block below ALWAYS runs after the init — an array of a
            // hook class fills via emitArrayFromTuple, then still needs its
            // elements' ctors fired.
            if (!stmt.children.empty()) {
                widen::Type::Form dform = widen::form(widen::strip(it->second.slids_type));
                widen::Type::Form sform = widen::form(widen::strip(stmt.children[0]->inferred_type));
                if (dform == widen::Type::Form::kArray
                    && stmt.children[0]->kind == ast::Kind::kTupleExpr) {
                    emitArrayFromTuple(it->second.alloca_name, it->second.slids_type,
                                       *stmt.children[0], syms, pool, out, diag);
                } else {
                    // A cross-form aggregate VALUE copy is lowered by slot in
                    // desugar; what remains here is a same-shape leaf-widen copy
                    // (or a const decl desugar skips) — load the source aggregate
                    // and walk per-leaf with the form-agnostic implicit convert.
                    // Shape already validated by classify::checkAggregateShapeMatch.
                    bool agg_widen =
                        (dform == widen::Type::Form::kArray
                         || dform == widen::Type::Form::kTuple)
                        && (sform == widen::Type::Form::kArray
                            || sform == widen::Type::Form::kTuple)
                        && widen::deepStrip(it->second.slids_type)
                            != widen::deepStrip(stmt.children[0]->inferred_type);
                    if (stmt.default_move_init && isAstLvalue(*stmt.children[0])) {
                        // A MOVE-INIT (`T x <-- y`) from an lvalue: compute the source
                        // address ONCE, then load + (widen?) + store + null its pointer
                        // leaves through it, so a side-effecting source index runs once.
                        // (desugar skips default_move_init, so a cross-form / leaf-widen
                        // default-move-init reaches here too — the convert keeps it correct.)
                        std::string src = emitLvalueAddr(*stmt.children[0], syms, pool,
                                                         out, diag, /*allow_partial=*/true);
                        std::string raw = newTmp("mv");
                        out << "  " << raw << " = load "
                            << llvmForRef(stmt.children[0]->inferred_type)
                            << ", ptr " << src << "\n";
                        std::string val;
                        if (agg_widen) {
                            val = emitImplicitAggregateConvert(
                                raw, stmt.children[0]->inferred_type,
                                it->second.slids_type, stmt.children[0]->file_id,
                                stmt.children[0]->tok, out, diag);
                        } else if (widen::deepStrip(stmt.children[0]->inferred_type)
                                   != widen::deepStrip(it->second.slids_type)) {
                            val = widen::convert(raw, stmt.children[0]->inferred_type,
                                                 it->second.slids_type,
                                                 stmt.children[0]->file_id,
                                                 stmt.children[0]->tok, out, diag);
                        } else {
                            val = raw;
                        }
                        out << "  store " << it->second.llvm_type << " " << val
                            << ", ptr " << it->second.alloca_name << "\n";
                        emitNullLeaves(src, stmt.children[0]->inferred_type, out);
                    } else {
                        // A copy (or a default-move-init from an RVALUE — no leaves to null).
                        std::string val;
                        if (agg_widen) {
                            val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                           widen::kNoType);
                            val = emitImplicitAggregateConvert(
                                val, stmt.children[0]->inferred_type,
                                it->second.slids_type,
                                stmt.children[0]->file_id, stmt.children[0]->tok,
                                out, diag);
                        } else {
                            val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                                           it->second.slids_type);
                        }
                        out << "  store " << it->second.llvm_type << " " << val
                            << ", ptr " << it->second.alloca_name << "\n";
                    }
                }
            }
            // Class lifecycle: every field is initialized before the constructor
            // hook runs (the field-init above). Then register the instance so its
            // destructor runs at scope exit, in reverse declaration order.
            {
                // Run construction hooks and register for destruction — for a
                // class, OR an array / tuple containing one (typeNeedsHook recurses).
                // ctor and dtor hooks are language-PAIRED (a class defines both or
                // neither; a synthesized class runs both to drive its fields), so a
                // SINGLE flag governs construction AND destructor registration —
                // they can never diverge into a leak (built, not registered) or an
                // orphan dtor (registered, never built).
                widen::TypeRef st = widen::strip(it->second.slids_type);
                bool needs_hooks = typeNeedsHook(st, /*ctor=*/true);
                assert(needs_hooks == typeNeedsHook(st, /*ctor=*/false)
                       && "ctor/dtor hook need diverged — must be language-paired");
                if (needs_hooks) {
                    // Every init runs the ctor after field-init — a fresh
                    // construction (from values) AND a copy/move (from a same-type
                    // whole value). So a copied/moved object is constructed exactly
                    // once, balancing its destructor at scope exit.
                    emitConstructHooks(it->second.alloca_name, st, out);
                    // NRVO: the returned local IS the caller's slot (it->second
                    // points at %sret.in) — construct it, but the CALLER owns its
                    // destruction, so don't register it here.
                    if (!stmt.nrvo) {
                        assert(scope && "class instance constructed without a dtor "
                                        "scope — it would never be destroyed");
                        scope->objs.push_back({it->second.alloca_name, st});
                    }
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
            // sret CALL into an existing var (`obj = fn()`). Phase B case 2 — an
            // existing POD target (no leaf ctor) of the exact type is OVERWRITTEN in
            // place: the callee builds straight into it, no temp.
            if (!stmt.children.empty()
                && stmt.children[0]->kind == ast::Kind::kCallExpr
                && isSretReturn(it->second.slids_type)
                && !typeNeedsHook(it->second.slids_type, /*ctor=*/true)
                && widen::deepStrip(it->second.slids_type)
                       == widen::deepStrip(stmt.children[0]->return_type)) {
                emitCall(*stmt.children[0], syms, pool, out, diag,
                         it->second.alloca_name);
                return;
            }
            // case 3 FALLBACK — a hook target is already constructed, so it can't be
            // rebuilt in place: temp + default-move-ASSIGN (whole-value overwrite +
            // null source), then destroy the temp husk.
            if (!stmt.children.empty()
                && stmt.children[0]->kind == ast::Kind::kCallExpr
                && typeNeedsHook(it->second.slids_type, /*ctor=*/true)) {
                widen::TypeRef T = it->second.slids_type;
                // The callee writes its return type into `slot` (sized as T); a class
                // never converts, so the two must be identical. A non-exact hook
                // assign would silently store the wrong layout — assert, don't.
                assert(widen::deepStrip(T)
                           == widen::deepStrip(stmt.children[0]->return_type)
                    && "hook sret assign must be exact-typed (no class conversion)");
                std::string Tll = it->second.llvm_type;
                // The result temp is reclaimed each time (stacksave/restore) so this
                // assign in a loop doesn't grow the stack per iteration.
                std::string sp = newTmp("sp");
                out << "  " << sp << " = call ptr @llvm.stacksave.p0()\n";
                std::string slot = newTmp("rettmp");
                out << "  " << slot << " = alloca " << Tll << "\n";
                emitCall(*stmt.children[0], syms, pool, out, diag, slot);
                std::string raw = newTmp("mv");
                out << "  " << raw << " = load " << Tll << ", ptr " << slot << "\n";
                out << "  store " << Tll << " " << raw << ", ptr "
                    << it->second.alloca_name << "\n";
                emitNullLeaves(slot, T, out);
                emitDestructHooks(slot, widen::strip(T), out);
                out << "  call void @llvm.stackrestore.p0(ptr " << sp << ")\n";
                return;
            }
            if (widen::form(widen::strip(it->second.slids_type))
                    == widen::Type::Form::kArray
                && stmt.children[0]->kind == ast::Kind::kTupleExpr) {
                emitArrayFromTuple(it->second.alloca_name, it->second.slids_type,
                                   *stmt.children[0], syms, pool, out, diag);
                return;
            }
            // A cross-form aggregate VALUE assign is lowered by slot in desugar;
            // what reaches here is a same-shape leaf-widen copy — handled by the
            // form-agnostic implicit convert below (see the kVarDeclStmt arm).
            widen::Type::Form a_dform =
                widen::form(widen::strip(it->second.slids_type));
            widen::Type::Form a_sform =
                widen::form(widen::strip(stmt.children[0]->inferred_type));
            bool a_agg_widen =
                (a_dform == widen::Type::Form::kArray
                 || a_dform == widen::Type::Form::kTuple)
                && (a_sform == widen::Type::Form::kArray
                    || a_sform == widen::Type::Form::kTuple)
                && widen::deepStrip(it->second.slids_type)
                    != widen::deepStrip(stmt.children[0]->inferred_type);
            std::string val;
            if (a_agg_widen) {
                val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                               widen::kNoType);
                val = emitImplicitAggregateConvert(
                    val, stmt.children[0]->inferred_type, it->second.slids_type,
                    stmt.children[0]->file_id, stmt.children[0]->tok, out, diag);
            } else {
                val = emitExpr(*stmt.children[0], syms, pool, out, diag,
                               it->second.slids_type);
            }
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
            // A sub-array store target (`grid[1] = ...`) is a PARTIAL index — allow
            // it to address the slice (a full/scalar index is the common case).
            bool sub_array = widen::form(widen::strip(elem))
                                 == widen::Type::Form::kArray;
            std::string addr;
            if (lvalue.kind == ast::Kind::kIndexExpr) {
                addr = emitElementAddr(lvalue, syms, pool, out, diag,
                                       /*allow_partial=*/sub_array);
            } else {
                assert(lvalue.kind == ast::Kind::kDerefExpr
                    && "kStoreStmt: lvalue must be a deref or index");
                ast::Node const& ptr_expr = *lvalue.children[0];
                addr = emitExpr(ptr_expr, syms, pool, out, diag,
                                ptr_expr.inferred_type);
            }
            ast::Node const& rhs = *stmt.children[1];
            // A SUB-ARRAY store target (a partial array index is array-typed): fill
            // the slice from a tuple LITERAL via the array<->tuple bridge — `addr`
            // is the slice's pointer. A tuple VALUE source is lowered by slot in
            // desugar; an array-value source falls through to the whole-value store.
            if (widen::form(widen::strip(elem)) == widen::Type::Form::kArray) {
                if (rhs.kind == ast::Kind::kTupleExpr) {
                    emitArrayFromTuple(addr, elem, rhs, syms, pool, out, diag);
                    return;
                }
            }
            // Per-element implicit widening when both sides are aggregate but
            // elem/slot types differ (classify validated shape via
            // checkAggregateShapeMatch). Same shape as kVarDeclStmt/kAssignStmt.
            widen::Type::Form st_dform = widen::form(widen::strip(elem));
            widen::Type::Form st_sform =
                widen::form(widen::strip(rhs.inferred_type));
            bool st_agg_widen =
                (st_dform == widen::Type::Form::kArray
                 || st_dform == widen::Type::Form::kTuple)
                && (st_sform == widen::Type::Form::kArray
                    || st_sform == widen::Type::Form::kTuple)
                && widen::deepStrip(elem)
                    != widen::deepStrip(rhs.inferred_type);
            std::string val;
            if (st_agg_widen) {
                val = emitExpr(rhs, syms, pool, out, diag, widen::kNoType);
                val = emitImplicitAggregateConvert(
                    val, rhs.inferred_type, elem, rhs.file_id, rhs.tok, out, diag);
            } else {
                val = emitExpr(rhs, syms, pool, out, diag, elem);
            }
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
            std::string dst = emitLvalueAddr(lhs, syms, pool, out, diag,
                                             /*allow_partial=*/true);
            std::string llty = llvmForRef(lhs.inferred_type);
            // A cross-form / leaf-widen aggregate move is lowered BY SLOT in desugar
            // (lowerAggCopyStmt); what reaches here is a SAME-type whole-value move.
            if (isAstLvalue(rhs)) {
                // Compute the source address ONCE, then LOAD the value AND null its
                // pointer leaves through it — a side-effecting source index
                // (`a <-- g[bump()]`) runs once. Aggregate moves here are same-type
                // (cross-form / leaf-widen are desugared), but a SCALAR move may widen,
                // so load at the SOURCE type and convert.
                std::string src = emitLvalueAddr(rhs, syms, pool, out, diag,
                                                 /*allow_partial=*/true);
                std::string raw = newTmp("mv");
                out << "  " << raw << " = load " << llvmForRef(rhs.inferred_type)
                    << ", ptr " << src << "\n";
                std::string val =
                    widen::deepStrip(rhs.inferred_type)
                        == widen::deepStrip(lhs.inferred_type)
                    ? raw
                    : widen::convert(raw, rhs.inferred_type, lhs.inferred_type,
                                     rhs.file_id, rhs.tok, out, diag);
                out << "  store " << llty << " " << val << ", ptr " << dst << "\n";
                emitNullLeaves(src, rhs.inferred_type, out);
            } else {
                // An rvalue source has no storage to null — evaluate once.
                std::string val = emitExpr(rhs, syms, pool, out, diag, lhs.inferred_type);
                out << "  store " << llty << " " << val << ", ptr " << dst << "\n";
            }
            return;
        }
        case ast::Kind::kSwapStmt: {
            // `a <--> b;` — exchange two same-type lvalues via SSA temporaries
            // (no stack temp). A whole-value load/store handles tuples and arrays
            // too. Both addresses and loads precede either store, so an aliased
            // swap is safe.
            assert(stmt.children.size() == 2 && "kSwapStmt needs two operands");
            ast::Node const& a = *stmt.children[0];
            ast::Node const& b = *stmt.children[1];
            std::string ll = llvmForRef(a.inferred_type);
            std::string addr_a = emitLvalueAddr(a, syms, pool, out, diag,
                                                /*allow_partial=*/true);
            std::string addr_b = emitLvalueAddr(b, syms, pool, out, diag,
                                                /*allow_partial=*/true);
            std::string va = newTmp("swap");
            std::string vb = newTmp("swap");
            out << "  " << va << " = load " << ll << ", ptr " << addr_a << "\n";
            out << "  " << vb << " = load " << ll << ", ptr " << addr_b << "\n";
            out << "  store " << ll << " " << vb << ", ptr " << addr_a << "\n";
            out << "  store " << ll << " " << va << ", ptr " << addr_b << "\n";
            return;
        }
        case ast::Kind::kDestructureStmt: {
            // (a, (b,c), ) = tuple. Evaluate the rhs aggregate ONCE, then bind each
            // slot (recursing into nested slots, discarding null ones).
            ast::Node const& rhs = *stmt.children[0];
            std::string agg = emitExpr(rhs, syms, pool, out, diag,
                                       rhs.inferred_type);
            emitDestructure(stmt, agg, rhs.inferred_type, syms, out);
            return;
        }
        case ast::Kind::kDeleteStmt: {
            // delete <ptr>; — free the pointer (running the dtor / per-element dtors
            // for a hook pointee), then null the operand back IFF it is an lvalue.
            // The operand is any pointer EXPRESSION (classify checked the type): a
            // variable / field / array element / tuple slot / deref is an LVALUE
            // (nulled back); a call return / op result is an RVALUE (freed only).
            ast::Node const& operand = *stmt.children[0];
            widen::TypeRef pts = widen::strip(operand.inferred_type);
            widen::Type::Form pf = widen::form(pts);
            widen::TypeRef pointee = (pf == widen::Type::Form::kPointer
                                   || pf == widen::Type::Form::kIterator)
                ? widen::get(pts).pointee : widen::kNoType;
            bool needs = pointee != widen::kNoType
                && typeNeedsHook(pointee, /*ctor=*/false);
            // The pointer value to free: from the lvalue's address (computed ONCE so
            // a side-effecting operand like `arr[bump()]` runs once), or evaluated
            // directly for an rvalue.
            bool del_lvalue = isAstLvalue(operand);
            std::string del_addr;
            std::string p;
            if (del_lvalue) {
                del_addr = emitLvalueAddr(operand, syms, pool, out, diag);
                p = newTmp("del");
                out << "  " << p << " = load ptr, ptr " << del_addr << "\n";
            } else {
                p = emitExpr(operand, syms, pool, out, diag, operand.inferred_type);
            }
            if (pf == widen::Type::Form::kIterator && needs) {
                // Array of a hook class: the element count is in an 8-byte cookie at
                // ptr-8 (laid down by new[]). Destroy each element in REVERSE, then
                // free the cookie (the original allocation). Null-guarded — free(null)
                // is safe but reading ptr-8 is not.
                std::string nn = newTmp("dnn");
                std::string dbody = newLabel("del_body");
                std::string dend = newLabel("del_end");
                out << "  " << nn << " = icmp ne ptr " << p << ", null\n";
                out << "  br i1 " << nn << ", label %" << dbody << ", label %"
                    << dend << "\n";
                out << dbody << ":\n";
                std::string hdr = newTmp("dhdr");
                out << "  " << hdr << " = getelementptr i8, ptr " << p
                    << ", i64 -8\n";
                std::string cnt = newTmp("dcnt");
                out << "  " << cnt << " = load i64, ptr " << hdr << "\n";
                std::string ll = llvmForRef(pointee);
                std::string cnd = newLabel("deld_cond");
                std::string lb = newLabel("deld_body");
                std::string le = newLabel("deld_end");
                out << "  br label %" << cnd << "\n";
                out << cnd << ":\n";
                std::string i = newTmp("di");
                std::string iprev = newTmp("diprev");
                out << "  " << i << " = phi i64 [ " << cnt << ", %" << dbody
                    << " ], [ " << iprev << ", %" << lb << " ]\n";
                std::string cmp = newTmp("dcmp");
                out << "  " << cmp << " = icmp ugt i64 " << i << ", 0\n";
                out << "  br i1 " << cmp << ", label %" << lb << ", label %"
                    << le << "\n";
                out << lb << ":\n";
                out << "  " << iprev << " = sub i64 " << i << ", 1\n";
                std::string elem = newTmp("delem");
                out << "  " << elem << " = getelementptr " << ll << ", ptr " << p
                    << ", i64 " << iprev << "\n";
                emitDestructHooks(elem, pointee, out);
                out << "  br label %" << cnd << "\n";
                out << le << ":\n";
                out << "  call void @free(ptr " << hdr << ")\n";
                out << "  br label %" << dend << "\n";
                out << dend << ":\n";
            } else {
                // Single object (T^): run the dtor before freeing. Null-guard the
                // dtor — free(null) is a safe no-op, but running the dtor on null
                // would deref it. (`delete p` on an already-null pointer is legal.)
                if (pf == widen::Type::Form::kPointer && needs) {
                    std::string nn = newTmp("dnn");
                    std::string db = newLabel("del_body");
                    std::string de = newLabel("del_end");
                    out << "  " << nn << " = icmp ne ptr " << p << ", null\n";
                    out << "  br i1 " << nn << ", label %" << db << ", label %"
                        << de << "\n";
                    out << db << ":\n";
                    emitDestructHooks(p, pointee, out);
                    out << "  br label %" << de << "\n";
                    out << de << ":\n";
                }
                out << "  call void @free(ptr " << p << ")\n";
            }
            // Null the operand back — only an lvalue has storage to null (an rvalue
            // pointer was a temporary).
            if (del_lvalue)
                out << "  store ptr null, ptr " << del_addr << "\n";
            return;
        }
        case ast::Kind::kDtorCallStmt: {
            // lvalue.~(); — run the receiver's destructor in place, NO free
            // (placement cleanup; the buffer is reclaimed separately). The receiver
            // is a class lvalue; its address is what the dtor runs on.
            ast::Node const& recv = *stmt.children[0];
            std::string addr = emitLvalueAddr(recv, syms, pool, out, diag);
            emitDestructHooks(addr, recv.inferred_type, out);
            return;
        }
        case ast::Kind::kCallStmt: {
            if (print::tryEmitCall(stmt, syms, pool, out, diag)) return;
            // A DISCARDED sret call still constructs its result into a slot — the
            // caller owns it, so destroy it here (otherwise the slot leaks and its
            // ctor goes unbalanced). Materialize the slot, call into it, dtor it.
            if (typeNeedsHook(stmt.return_type, /*ctor=*/true)) {
                widen::TypeRef T = stmt.return_type;
                // Reclaim the temp each time so a discarded call in a loop doesn't
                // grow the stack per iteration.
                std::string sp = newTmp("sp");
                out << "  " << sp << " = call ptr @llvm.stacksave.p0()\n";
                std::string slot = newTmp("rettmp");
                out << "  " << slot << " = alloca " << llvmForRef(T) << "\n";
                emitCall(stmt, syms, pool, out, diag, slot);
                emitDestructHooks(slot, widen::strip(T), out);
                out << "  call void @llvm.stackrestore.p0(ptr " << sp << ")\n";
                return;
            }
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
            // Materialize the return value FIRST, then destroy every in-scope
            // class instance (innermost-out — the whole chain), then return.
            if (stmt.children.empty()) {
                emitUnwindDtors(scope, nullptr, out);
                out << "  ret void\n";
                return;
            }
            ast::Node const& rv = *stmt.children[0];
            std::string llty = llvmForRef(fn_return_type);
            if (isSretReturn(fn_return_type)) {
                // NRVO: the returned local WAS built directly in %sret.in (its
                // storage is the slot) and is not in our dtor scope — nothing to
                // move or construct; just unwind the OTHER locals and return.
                if (stmt.nrvo) {
                    emitUnwindDtors(scope, nullptr, out);
                    out << "  ret void\n";
                    return;
                }
                // Both paths below build `rv` into %sret.in at the return type with
                // NO conversion. Desugar lowers a non-exact (cross-form / leaf-widen)
                // aggregate return to an exact `_$ret` temp, and a class never
                // converts — so what reaches here is exact-typed. Assert it: a
                // regression would silently load/store at the wrong type.
                {
                    widen::TypeRef rvt = (rv.kind == ast::Kind::kCallExpr)
                        ? rv.return_type : rv.inferred_type;
                    assert((rvt == widen::kNoType
                            || widen::deepStrip(rvt)
                                   == widen::deepStrip(fn_return_type))
                        && "sret return not exact-typed — desugar lowers non-exact");
                    (void)rvt;
                }
                // RETURN-OF-CALL: forward our slot to the callee, which constructs
                // its result DIRECTLY into %sret.in — no temp, no extra ctor.
                if (rv.kind == ast::Kind::kCallExpr) {
                    emitCall(rv, syms, pool, out, diag, "%sret.in");
                    emitUnwindDtors(scope, nullptr, out);
                    out << "  ret void\n";
                    return;
                }
                // sret FALLBACK: CONSTRUCT the result into the caller's slot
                // %sret.in by default-move-init (field-move the value in, then run
                // the slot's ctor), so the slot is constructed exactly once and the
                // ctor sees the returned values. The slot is NOT dtor-registered —
                // the caller owns it. A NAMED-local source is left a moved-from husk
                // and destroyed by the unwind below (the function-fallback's extra
                // object; Phase C NRVO will elide it).
                if (isAstLvalue(rv)) {
                    std::string src = emitLvalueAddr(rv, syms, pool, out, diag,
                                                     /*allow_partial=*/true);
                    std::string raw = newTmp("rv");
                    out << "  " << raw << " = load " << llty << ", ptr " << src << "\n";
                    out << "  store " << llty << " " << raw << ", ptr %sret.in\n";
                    emitNullLeaves(src, fn_return_type, out);
                } else {
                    std::string val = emitExpr(rv, syms, pool, out, diag, fn_return_type);
                    out << "  store " << llty << " " << val << ", ptr %sret.in\n";
                }
                emitConstructHooks("%sret.in", widen::strip(fn_return_type), out);
                emitUnwindDtors(scope, nullptr, out);
                out << "  ret void\n";
                return;
            }
            // A cross-form / leaf-widen aggregate return is lowered BY SLOT in
            // desugar (lowerAggCopyStmt materializes a `_$ret` temp of the return
            // type), so what reaches here matches the return type — emit it directly.
            std::string val = emitExpr(rv, syms, pool, out, diag, fn_return_type);
            emitUnwindDtors(scope, nullptr, out);
            out << "  ret " << llty << " " << val << "\n";
            return;
        }
        case ast::Kind::kBlockStmt: {
            // A nested lexical scope. Push a dtor scope so class instances declared
            // here are destroyed (reverse declaration order) when control leaves
            // the block. Allocas are still hoisted to the entry block; the SymTab
            // is entry-id-keyed, so this only adds destruction bookkeeping. Thread
            // the loop context so a break/continue reaches the enclosing loop.
            DtorScope block_scope{{}, scope};
            for (auto const& ch : stmt.children) {
                if (ch) emitStmt(*ch, syms, pool, fn_return_type, loop,
                                 &block_scope, out, diag);
            }
            // Normal fall-through exit destroys this scope. If the block ended
            // abruptly (return/break/continue), that statement already unwound
            // this scope and emitting after a terminator is invalid IR — skip.
            if (!endsTerminated(stmt.children)) emitScopeDtors(block_scope, out);
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
            emitStmt(*stmt.children[1], syms, pool, fn_return_type, loop, scope, out, diag);
            if (then_falls) out << "  br label %" << merge_lbl << "\n";

            if (has_else) {
                out << else_lbl << ":\n";
                emitStmt(*stmt.children[2], syms, pool, fn_return_type, loop, scope, out, diag);
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
            LoopCtx ctx{head_lbl, exit_lbl, loop, scope};
            emitStmt(*stmt.children[1], syms, pool, fn_return_type, &ctx, scope, out, diag);
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
            LoopCtx ctx{cond_lbl, exit_lbl, loop, scope};   // continue -> cond, break -> exit
            emitStmt(*stmt.children[1], syms, pool, fn_return_type, &ctx, scope, out, diag);
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
                             scope, out, diag);
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
            LoopCtx ctx{upd_lbl, exit_lbl, loop, scope};   // continue -> update, break -> exit
            emitStmt(*stmt.children[2], syms, pool, fn_return_type, &ctx, scope, out, diag);
            if (!endsTerminated(stmt.children[2]->children)) {
                out << "  br label %" << upd_lbl << "\n";
            }

            out << upd_lbl << ":\n";
            // The update can't break/continue/return (resolve-enforced), so it
            // always falls through back to the head. Pass the enclosing loop ctx
            // (a nested loop inside the update carries its own).
            emitStmt(*stmt.children[1], syms, pool, fn_return_type, loop, scope, out, diag);
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
            // Destroy every class instance in the scopes this jump unwinds — down
            // to (but not including) the scope outside the target loop.
            emitUnwindDtors(scope, t->scope, out);
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
            emitUnwindDtors(scope, t->scope, out);
            out << "  br label %" << t->header_label << "\n";
            return;
        }
        case ast::Kind::kSwitchStmt: {
            // children[0]=scrutinee, [1..]=kCaseClause (a label-list +
            // children.back()=body block; text=="continue" => trailing
            // fall-through). Lower to an `llvm switch` dispatching to one block per
            // clause + an exit. There is NO implicit fall-through: a clause brs to
            // the exit at its body's end unless it has a trailing continue, which
            // brs to the next clause (a continue on the last clause falls off the
            // bottom -> exit). break/continue inside a body bind to the enclosing
            // loop (switch is transparent), so the enclosing `loop` ctx is passed
            // straight through. default's block is the switch instr's default label
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
                ast::Node const& clause = *stmt.children[k + 1];
                std::size_t nlabel = clause.children.size() - 1;   // body = back()
                bool is_default = false;
                for (std::size_t j = 0; j < nlabel; j++)
                    if (!clause.children[j]) { is_default = true; break; }
                if (is_default) { default_lbl = blk[k]; break; }
            }
            out << "  switch " << llty << " " << sv << ", label %" << default_lbl
                << " [\n";
            for (std::size_t k = 0; k < n; k++) {
                ast::Node const& clause = *stmt.children[k + 1];
                std::size_t nlabel = clause.children.size() - 1;
                for (std::size_t j = 0; j < nlabel; j++) {
                    if (clause.children[j]) {
                        out << "    " << llty << " " << clause.children[j]->text
                            << ", label %" << blk[k] << "\n";
                    }
                }
            }
            out << "  ]\n";
            bool exit_reachable = (default_lbl == exit_lbl);   // no default clause
            for (std::size_t k = 0; k < n; k++) {
                ast::Node const& clause = *stmt.children[k + 1];
                ast::Node const& body = *clause.children.back();
                out << blk[k] << ":\n";
                emitStmt(body, syms, pool, fn_return_type, loop, scope, out, diag);
                if (!endsTerminated(body.children)) {
                    bool has_cont = (clause.text == "continue");
                    if (has_cont && k + 1 < n) {
                        out << "  br label %" << blk[k + 1] << "\n";   // fall through
                    } else {
                        out << "  br label %" << exit_lbl << "\n";     // exit switch
                        exit_reachable = true;
                    }
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
    // escaping break, and every clause's exit reaches a return. With no implicit
    // fall-through a non-returning clause is acceptable only if it has a trailing
    // continue into a LATER clause (the chain must end in a return).
    bool has_default = false;
    for (std::size_t i = 1; i < s.children.size(); i++) {
        ast::Node const& clause = *s.children[i];
        std::size_t nlabel = clause.children.size() - 1;
        for (std::size_t j = 0; j < nlabel; j++)
            if (!clause.children[j]) has_default = true;
        if (containsBreak(*clause.children.back())) return false;
    }
    if (!has_default) return false;
    for (std::size_t i = 1; i < s.children.size(); i++) {
        ast::Node const& clause = *s.children[i];
        if (endsInReturnNode(*clause.children.back())) continue;
        bool has_cont = (clause.text == "continue");
        bool is_last = (i + 1 == s.children.size());
        if (!has_cont || is_last) return false;
    }
    return true;
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
// alloca can be hoisted to the entry block (see emitFunction). Walks the whole
// subtree — statement bodies AND expression operands — because a PPID-lowered
// complex inc/dec hides a `_$lv` reference decl inside a kSeqExpr that sits in an
// expression position (a call arg, an rhs, a condition). A nested function owns
// its own locals, so its body is NOT descended.
void collectVarDecls(ast::Node const& s, std::vector<ast::Node const*>& out) {
    if (s.kind == ast::Kind::kFunctionDef || s.kind == ast::Kind::kFunctionDecl)
        return;
    if (s.kind == ast::Kind::kVarDeclStmt && !s.is_const) out.push_back(&s);
    for (auto const& ch : s.children) if (ch) collectVarDecls(*ch, out);
}

void emitFunction(ast::Node const& fn, strings::Pool& pool,
                  std::ostream& out, diagnostic::Sink& diag) {
    // A non-primitive (hook-bearing) return is lowered to sret: the function
    // returns void and takes a leading caller-provided slot `%sret.in` it
    // CONSTRUCTS the result into (see kReturnStmt). POD / primitive returns are
    // unchanged (returned by value).
    bool sret = isSretReturn(fn.return_type);
    std::string ret_llty = sret ? "void" : llvmForRef(fn.return_type);
    out << "define " << ret_llty << " @" << fn.name << "(";
    bool need_comma = false;
    if (sret) {
        out << "ptr %sret.in";
        need_comma = true;
    }
    for (size_t i = 0; i < fn.params.size(); i++) {
        ast::Node const& p = *fn.params[i];
        if (need_comma) out << ", ";
        std::string p_llty = llvmForRef(p.return_type);
        out << p_llty << " %arg." << i;
        need_comma = true;
    }
    // A lifted nested function takes one extra `ptr` arg per capture (the host
    // variable's address — by reference).
    for (size_t i = 0; i < fn.captures.size(); i++) {
        if (need_comma) out << ", ";
        out << "ptr %cap." << i;
        need_comma = true;
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
        // NRVO: the returned local's storage IS the caller's slot — no alloca; its
        // construction (and reads/writes) go straight through %sret.in.
        if (d->nrvo) {
            syms[d->resolved_entry_id] = {"%sret.in", llty, d->return_type};
            continue;
        }
        std::string regname = std::string("%") + d->name + "."
            + std::to_string(d->resolved_entry_id);
        out << "  " << regname << " = alloca " << llty << "\n";
        syms[d->resolved_entry_id] = {regname, llty, d->return_type};
    }
    // The function body is the outermost dtor scope: a class instance declared
    // at top level is destroyed at the function's exit. An explicit `return`
    // unwinds this scope itself; the implicit fall-through below does it here.
    DtorScope root_scope;
    for (auto const& s : fn.children) {
        emitStmt(*s, syms, pool, fn.return_type, /*loop=*/nullptr, &root_scope,
                 out, diag);
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
        emitScopeDtors(root_scope, out);   // destroy function-scope instances
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
            if (diagnostic::hasErrors(diag)) break;   // stop at the first error
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
        if (diagnostic::hasErrors(diag)) break;   // stop at the first error
        emitFunction(*fn, pool, body, diag);
    }
    // Per-class size helper. LLVM owns the struct layout, so the byte size is the
    // GEP-null/ptrtoint of the struct type — emitted as `<Name>__$sizeof()` (v1's
    // design), which sizeof(Class) (and new/delete) call. A function rather than an
    // inline expression so it resolves at link time for cross-TU classes (Phase 8).
    for (widen::TypeRef ct : tree.classes) {
        if (diagnostic::hasErrors(diag)) break;
        std::string llty = llvmForRef(ct);
        std::string name = widen::classSymbol(ct);
        body << "define internal i64 @" << name << "__$sizeof() {\n";
        body << "  %g = getelementptr " << llty << ", ptr null, i32 1\n";
        body << "  %s = ptrtoint ptr %g to i64\n";
        body << "  ret i64 %s\n";
        body << "}\n\n";
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
