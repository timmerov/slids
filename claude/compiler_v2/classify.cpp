#include "classify.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "diagnostic.h"
#include "parse.h"
#include "widen.h"

namespace classify {

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

bool isLiteralKind(parse::Kind k) {
    return k == parse::Kind::kIntLiteral
        || k == parse::Kind::kUintLiteral
        || k == parse::Kind::kCharLiteral
        || k == parse::Kind::kBoolLiteral
        || k == parse::Kind::kFloatLiteral;
}

// The preferred user-facing spelling for an inferred-from-literal type: the
// 32-bit defaults read as int / uint / float (their narrow preferred names), so
// `a = 42` infers `int` (matching an explicitly-typed sibling's ##type), not the
// internal `int32`. Other widths pass through unchanged.
std::string preferredSpelling(std::string const& t) {
    if (t == "int32")   return "int";
    if (t == "uint32")  return "uint";
    if (t == "float32") return "float";
    return t;
}

bool isFloatType(std::string const& t) {
    return t == "float" || t == "float32" || t == "float64";
}

// A reference type spelling: `T^`.
bool isReference(std::string const& t) {
    return widen::form(widen::intern(t)) == widen::Type::Form::kPointer;
}

// An iterator type spelling: `T[]`.
bool isIteratorType(std::string const& t) {
    return widen::form(widen::intern(t)) == widen::Type::Form::kIterator;
}

// Any pointer: an iterator (`T[]`), a reference (`T^`), or the typeless null
// (`anyptr`, nullptr's type). Used for truthy-coercion and pointer ops.
bool isPtrLikeType(std::string const& t) {
    widen::Type::Form f = widen::form(widen::intern(t));
    return f == widen::Type::Form::kPointer
        || f == widen::Type::Form::kIterator
        || f == widen::Type::Form::kAnyptr;
}

// The pointee type of a reference/iterator; empty for anyptr or a non-pointer.
std::string pointeeType(std::string const& t) {
    widen::Type const& ty = widen::get(widen::intern(t));
    if (ty.form == widen::Type::Form::kPointer
     || ty.form == widen::Type::Form::kIterator) {
        return widen::spell(ty.pointee);
    }
    return "";
}

bool isNumericType(std::string const& t);   // defined below

// A buffer-class pointer: a pointer (reference or iterator) whose pointee is a
// generic byte type — `void`, `int8`, `uint8`. These reinterpret to/from any
// pointer type; every other pointer pair must reinterpret indirectly (chain
// through a buffer-class type). `void` is included though it only spells as a
// reference (`void^`): void has no stride, so `void[]` never reaches here.
bool isBufferClassPtr(std::string const& t) {
    std::string p = pointeeType(t);
    return p == "void" || p == "int8" || p == "uint8";
}

// The pointee of a reference / iterator (used below for same-pointee checks).
std::string castPointee(std::string const& t) {
    return pointeeType(t);
}

// May a value of type `from` be IMPLICITLY assigned to a pointer-or-intptr
// lvalue of type `to`? Implicit pointer casts only ever STRIP type information
// (the widening direction): nullptr → any pointer, any pointer → `void^` or
// `intptr`, and an iterator demoted to a reference of the same pointee. Adding
// information (`void^`/`intptr` → a typed pointer, reference → iterator) needs
// an explicit `<Type^>`. Caller gates on either side being pointer-ish.
bool ptrImplicitOk(std::string const& from, std::string const& to) {
    if (from == to) return true;
    if (from == "anyptr") return isPtrLikeType(to);   // nullptr → any pointer
    if (to == "void^")    return isPtrLikeType(from);  // strip to a void reference
    if (to == "intptr")   return isPtrLikeType(from);  // strip to an integer
    // An iterator demotes to a reference of the same pointee (loses arithmetic).
    if (isReference(to) && isIteratorType(from)
        && castPointee(to) == castPointee(from)) return true;
    return false;
}

// Is `t` a legal cast endpoint — a pointer, or the integer type `intptr`? Only
// `intptr` bridges pointers and integers; no other integer type may be cast
// to or from a pointer.
bool isCastEndpoint(std::string const& t) {
    return isPtrLikeType(t) || t == "intptr";
}

// May an EXPLICIT `<to> from` cast reinterpret a value of type `from` as `to`?
// Both endpoints must be a pointer or `intptr`. A buffer-class pointer (or
// `intptr`) on either side bridges to any pointer; an iterator and a reference
// of the same pointee reinterpret either way. Two unrelated non-buffer pointers
// may not cast directly (the canonical "chain through void^" rule). On failure,
// `why` is set to a user-facing reason.
bool ptrExplicitOk(std::string const& from, std::string const& to,
                   std::string& why) {
    if (isNumericType(from) && from != "intptr") {
        why = "only 'intptr' may be cast to or from a pointer";
        return false;
    }
    if (isNumericType(to) && to != "intptr") {
        why = "only 'intptr' may be cast to or from a pointer";
        return false;
    }
    if (!isCastEndpoint(from)) {
        why = "a cast operand must be a pointer or 'intptr'";
        return false;
    }
    if (!isCastEndpoint(to)) {
        why = "a cast target must be a pointer or 'intptr'";
        return false;
    }
    if (from == to) return true;
    if (from == "anyptr") return true;                 // nullptr → any pointer
    if (from == "intptr" || to == "intptr") return true;  // pointer ↔ integer
    if (isBufferClassPtr(from) || isBufferClassPtr(to)) return true;  // buffer ↔ any
    if (((isReference(from) && isIteratorType(to))
      || (isIteratorType(from) && isReference(to)))
        && castPointee(from) == castPointee(to)) return true;  // iterator ↔ reference
    why = "reinterpret indirectly through 'void^'";
    return false;
}

// A fixed-size array type spelling (`int[5]`, `int[3][5]`), distinct from
// `int[]` (iterator) and `int^` (ref).
bool isArrayType(std::string const& t) {
    return widen::form(widen::intern(t)) == widen::Type::Form::kArray;
}

// The leftmost (innermost) dimension's size — the dimension one subscript
// consumes. `int[3][5]` -> 3.
int arrayFirstDim(std::string const& t) {
    return widen::get(widen::intern(t)).dims.front();
}

// The type after one subscript: strip the leftmost `[N]`. `int[3][5]` ->
// `int[5]`; `int[5]` -> `int`.
std::string arrayElementType(std::string const& t) {
    widen::Type const& a = widen::get(widen::intern(t));
    std::string s = widen::spell(a.elem);
    for (std::size_t i = 1; i < a.dims.size(); i++)
        s += "[" + std::to_string(a.dims[i]) + "]";
    return s;
}

bool isIntegerClass(std::string const& t) {
    widen::TypeKind k;
    if (!widen::classify(t, k)) return false;
    return k.cat != widen::Category::kFloat;
}


bool isNumericType(std::string const& t) {
    widen::TypeKind k;
    return widen::classify(t, k);
}

// `!` and the logical operators truthy-coerce: numerics via cmp-against-zero,
// pointer-like via cmp-against-null. Void (and any future non-value type) is
// rejected here.
bool isCoercibleToBool(std::string const& t) {
    return isNumericType(t) || isPtrLikeType(t);
}

// Text suitable for widen::intLiteralFits / floatLiteralFits — bool "true"/"false"
// → "1"/"0"; everything else passes through.
std::string literalTextForFit(parse::Node const& n) {
    if (n.kind == parse::Kind::kBoolLiteral) return (n.text == "true") ? "1" : "0";
    return n.text;
}

// Per-kind default when a literal has no usable context.
std::string defaultLiteralType(parse::Node const& n) {
    switch (n.kind) {
        case parse::Kind::kIntLiteral: {
            std::string const& s = n.text;
            bool neg = !s.empty() && s[0] == '-';
            std::string mag_str = neg ? s.substr(1) : s;
            errno = 0;
            char* end = nullptr;
            uint64_t mag = std::strtoull(mag_str.c_str(), &end, 10);
            assert(!mag_str.empty() && end != mag_str.c_str() && *end == '\0'
                && errno != ERANGE
                && "defaultLiteralType: malformed int text from numeric");
            if (neg) {
                if (mag <= static_cast<uint64_t>(INT32_MAX) + 1) return "int32";
                return "int64";
            }
            if (mag <= static_cast<uint64_t>(INT32_MAX)) return "int32";
            if (mag <= static_cast<uint64_t>(INT64_MAX)) return "int64";
            return "uint64";
        }
        case parse::Kind::kUintLiteral: {
            std::string const& s = n.text;
            errno = 0;
            char* end = nullptr;
            uint64_t mag = std::strtoull(s.c_str(), &end, 10);
            assert(!s.empty() && end != s.c_str() && *end == '\0'
                && errno != ERANGE
                && "defaultLiteralType: malformed uint text from numeric");
            if (mag <= static_cast<uint64_t>(UINT32_MAX)) return "uint32";
            return "uint64";
        }
        case parse::Kind::kCharLiteral:  return "char";
        case parse::Kind::kBoolLiteral:  return "bool";
        case parse::Kind::kFloatLiteral: return "float32";
        case parse::Kind::kStringLiteral:
        case parse::Kind::kNullptrLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kStoreStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kCallExpr:
        case parse::Kind::kExprStmt:
        case parse::Kind::kAliasDecl:
        case parse::Kind::kNamespaceDecl:
        case parse::Kind::kEnumDecl:
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr:
        case parse::Kind::kAddrOfExpr:
        case parse::Kind::kDerefExpr:
        case parse::Kind::kIndexExpr:
        case parse::Kind::kCastExpr:
        case parse::Kind::kNewExpr:
        case parse::Kind::kDeleteStmt:
        case parse::Kind::kSizeofExpr:
        case parse::Kind::kStringifyType:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kBlockStmt:
        case parse::Kind::kIfStmt:
        case parse::Kind::kWhileStmt:
        case parse::Kind::kDoWhileStmt:
        case parse::Kind::kForLongStmt:
        case parse::Kind::kForEnumStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kSwitchStmt:
        case parse::Kind::kCaseClause:
        case parse::Kind::kParam:
            assert(false && "defaultLiteralType: not a literal kind");
            __builtin_unreachable();
    }
    assert(false && "defaultLiteralType: unhandled parse::Kind");
    __builtin_unreachable();
}

bool literalFitsContext(parse::Node const& lit, std::string const& context_type) {
    if (context_type.empty()) return false;
    if (lit.kind == parse::Kind::kFloatLiteral) {
        return widen::floatLiteralFits(lit.text, context_type);
    }
    return widen::intLiteralFits(literalTextForFit(lit), context_type);
}

void inferExpr(parse::Tree& tree, parse::Node& e,
               std::string const& context, diagnostic::Sink& diag);
void classifyFunctionBody(parse::Tree& tree, parse::Node& fn,
                          diagnostic::Sink& diag);
void classifyNamespace(parse::Tree& tree, parse::Node& node,
                       diagnostic::Sink& diag);

// Walk a left-leaning '+' chain in a print-intrinsic argument. Each leaf
// segment infers in isolation — '+' here is print's concatenation marker,
// not the arith operator.
void inferPrintArg(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    if (e.kind == parse::Kind::kBinaryExpr && e.text == "+"
        && e.children.size() == 2) {
        inferPrintArg(tree, *e.children[0], diag);
        inferPrintArg(tree, *e.children[1], diag);
        return;
    }
    inferExpr(tree, e, "", diag);
}

// Alias-label propagation for an arith/bitwise binary. An alias is sticky against
// itself or a const literal (which flexes into the named partner), and drops to
// its underlying (empty label) against any other typed operand:
//   Integer + Integer -> Integer;  Integer + 1 -> Integer;  Integer + int -> int.
std::string binaryLabel(parse::Node const& lhs, parse::Node const& rhs) {
    bool lhs_lit = isLiteralKind(lhs.kind);
    bool rhs_lit = isLiteralKind(rhs.kind);
    if (!lhs.alias_label.empty()
        && (rhs_lit || lhs.alias_label == rhs.alias_label)) {
        return lhs.alias_label;
    }
    if (!rhs.alias_label.empty() && lhs_lit) return rhs.alias_label;
    return "";
}

// Literal-flex preamble for non-shift binaries: when one operand is a literal
// and the other is not, try-flex the literal into the partner's type.
void flexBinaryOperands(parse::Node& lhs, parse::Node& rhs) {
    bool lhs_lit = isLiteralKind(lhs.kind);
    bool rhs_lit = isLiteralKind(rhs.kind);
    if (lhs_lit && !rhs_lit && !rhs.inferred_type.empty()) {
        if (literalFitsContext(lhs, rhs.inferred_type)) {
            lhs.inferred_type = rhs.inferred_type;
        }
    } else if (rhs_lit && !lhs_lit && !lhs.inferred_type.empty()) {
        if (literalFitsContext(rhs, lhs.inferred_type)) {
            rhs.inferred_type = lhs.inferred_type;
        }
    }
}

// Two types occupy the same width-class (so `int` and `int32` are "the same" for
// overload-exactness, sidestepping the int/int32 spelling). Non-numeric types
// (char[], slids) fall back to string equality.
bool sameClass(std::string const& a, std::string const& b) {
    widen::TypeKind ka, kb;
    if (!widen::classify(a, ka) || !widen::classify(b, kb)) return a == b;
    return ka.cat == kb.cat && ka.bits == kb.bits;
}

// Conversion cost of argument `a` to parameter type `param` for overload ranking:
// 0 = exact (same class), 1 = a widening (literal flex, or within-family widen),
// -1 = not convertible (narrowing / cross-family / un-typeable).
int argConvertCost(parse::Node const& a, std::string const& param) {
    std::string const& at = a.inferred_type;
    if (at.empty()) return -1;
    if (sameClass(at, param)) return 0;
    if (isLiteralKind(a.kind)) {
        return literalFitsContext(a, param) ? 1 : -1;
    }
    std::string out;
    if (widen::commonType(at, param, out) && sameClass(out, param)) return 1;
    return -1;
}

// Resolve a (possibly overloaded) user-function call: infer args, pick the best
// candidate, stamp the chosen signature. Defined after inferExpr (mutually
// recursive — it infers the argument expressions).
void classifyCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);
void checkStrongConstAssign(std::string const& dest, parse::Node const& rhs,
                            diagnostic::Sink& diag);

void inferExpr(parse::Tree& tree, parse::Node& e,
               std::string const& context, diagnostic::Sink& diag) {
    switch (e.kind) {
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral: {
            if (literalFitsContext(e, context)) {
                e.inferred_type = context;
            } else {
                e.inferred_type = defaultLiteralType(e);
            }
            return;
        }
        case parse::Kind::kStringLiteral: {
            e.inferred_type = "char[]";
            return;
        }
        case parse::Kind::kNullptrLiteral: {
            // nullptr takes the pointer type in context (`int^ p = nullptr`);
            // with no pointer context it is the typeless null `anyptr`, which
            // coerces in comparisons against any pointer.
            e.inferred_type = isPtrLikeType(context) ? context : "anyptr";
            return;
        }
        case parse::Kind::kAddrOfExpr: {
            // `^lvalue` -> a pointer to the operand. The category is operand-
            // driven: a bare variable yields a reference (`T^`); an indexed
            // array element yields an iterator (`T[]`).
            assert(e.children.size() == 1 && "kAddrOfExpr needs 1 operand");
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, "", diag);
            if (!operand.inferred_type.empty()) {
                bool indexed = (operand.kind == parse::Kind::kIndexExpr);
                e.inferred_type = operand.inferred_type + (indexed ? "[]" : "^");
            }
            return;
        }
        case parse::Kind::kIndexExpr: {
            // `base[index]` -> an element. The base must be an array; the result
            // strips one (leftmost) dimension. A constant index is bounds-checked.
            assert(e.children.size() == 2 && "kIndexExpr needs base + index");
            parse::Node& base = *e.children[0];
            parse::Node& index = *e.children[1];
            inferExpr(tree, base, "", diag);
            inferExpr(tree, index, "", diag);
            std::string const& bt = base.inferred_type;
            bool array = isArrayType(bt);
            bool iter = isIteratorType(bt);
            if (!bt.empty() && !array && !iter) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Cannot subscript a non-array value of type '" + bt + "'.",
                    {}});
                return;
            }
            if (!index.inferred_type.empty()
                && !isIntegerClass(index.inferred_type)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "An array index must be an integer; got '"
                    + index.inferred_type + "'.", {}});
            }
            // A constant integer index is bounds-checked against a fixed-size
            // array; an iterator has no known length, so no check. char literals
            // carry their numeric codepoint as text (so a `char` index counts).
            if (array
                && (index.kind == parse::Kind::kIntLiteral
                    || index.kind == parse::Kind::kUintLiteral
                    || index.kind == parse::Kind::kCharLiteral)) {
                long idx = std::strtol(index.text.c_str(), nullptr, 10);
                int dim = arrayFirstDim(bt);
                if (idx < 0 || idx >= dim) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Array index " + std::to_string(idx)
                        + " is out of bounds for '" + bt + "'.", {}});
                }
            }
            e.inferred_type = bt.empty() ? std::string()
                            : array       ? arrayElementType(bt)
                                          : pointeeType(bt);   // iterator element
            return;
        }
        case parse::Kind::kDerefExpr: {
            // `value^` -> the pointee. The operand must be a pointer.
            assert(e.children.size() == 1 && "kDerefExpr needs 1 operand");
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, "", diag);
            std::string const& ot = operand.inferred_type;
            if (!ot.empty() && !isPtrLikeType(ot)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Cannot dereference a non-pointer value of type '"
                    + ot + "'.", {}});
                return;
            }
            e.inferred_type = pointeeType(ot);
            return;
        }
        case parse::Kind::kSizeofExpr: {
            // sizeof(...) -> the operand's byte size as an `intptr`. constfold
            // already folded the statically-known operands (type, string,
            // nullptr, address-of, a plain ident) so they can feed compile-time-
            // constant contexts (a const initializer, an array dimension). What
            // reaches HERE is the residual that needs type inference — a deref,
            // an index, an arithmetic expression — plus a slid-type operand
            // (typeByteSize -1) that constfold left for this arm to reject. The
            // computed size is frozen into a kIntLiteral typed `intptr`.
            // A string literal is special: its byte length INCLUDING the null,
            // not sizeof(char[]) = 8.
            long long size;
            if (e.children.empty()
                || e.children[0]->kind != parse::Kind::kStringLiteral) {
                std::string ty = e.return_type;
                if (ty.empty()) {
                    parse::Node& operand = *e.children[0];
                    inferExpr(tree, operand, "", diag);
                    ty = operand.inferred_type;
                }
                // An empty type rides an already-reported upstream error (an
                // un-typeable operand); fall to 0 silently rather than cascade.
                // A slid type (typeByteSize -1) is not statically sized — report;
                // unreachable until classes (Phase 5 / cross-TU) exist.
                size = ty.empty() ? 0 : widen::typeByteSize(ty);
                if (size < 0) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Cannot take sizeof of '" + ty + "'.", {}});
                    size = 0;
                }
            } else {
                size = static_cast<long long>(e.children[0]->text.size()) + 1;
            }
            e.kind = parse::Kind::kIntLiteral;
            e.text = std::to_string(size);
            e.return_type.clear();
            e.children.clear();
            e.inferred_type = "intptr";
            return;
        }
        case parse::Kind::kNewExpr: {
            // new T -> T^; new T[n] -> T[]. The array-size must be integer-class;
            // a placement address (children[1]) must be a buffer-class pointer;
            // the element type must be statically sized (Phase 4: a primitive — a
            // slid's size lands with classes).
            std::string const& elem = e.return_type;
            bool is_array = e.children[0] != nullptr;
            // The `!inferred_type.empty()` guards below skip an operand whose type
            // is empty (an upstream error already reported it) — don't cascade.
            if (is_array) {
                parse::Node& size = *e.children[0];
                inferExpr(tree, size, "intptr", diag);
                if (!size.inferred_type.empty()
                    && !isIntegerClass(size.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "An array size must be an integer; got '"
                        + size.inferred_type + "'.", {}});
                }
            }
            if (e.children[1]) {
                parse::Node& addr = *e.children[1];
                inferExpr(tree, addr, "", diag);
                if (!addr.inferred_type.empty()
                    && !isBufferClassPtr(addr.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "A placement address must be a buffer-class pointer "
                        "(void^, int8^, uint8^); got '" + addr.inferred_type
                        + "'.", {}});
                }
            }
            if (widen::typeByteSize(elem) < 0) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Cannot allocate '" + elem + "'.", {}});
            }
            e.inferred_type = elem + (is_array ? "[]" : "^");
            return;
        }
        case parse::Kind::kCastExpr: {
            // `<Type^> operand` — a pointer reinterpret. resolve resolved the
            // target onto return_type. Infer the operand with NO context (a cast
            // reinterprets the operand's own type; it does not flex it), then
            // check the explicit-cast rules. The cast's type IS the target.
            assert(e.children.size() == 1 && "kCastExpr needs 1 operand");
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, "", diag);
            std::string const& to = e.return_type;
            // An empty operand type means inferExpr already reported an error;
            // skip the rule check (it would cascade a second, misleading
            // diagnostic) but still stamp the target type so downstream stages
            // see a typed node — the cast's type IS the target regardless.
            if (!operand.inferred_type.empty()) {
                std::string why;
                if (!ptrExplicitOk(operand.inferred_type, to, why)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Cannot cast '" + operand.inferred_type + "' to '" + to
                        + "'; " + why + ".", {}});
                }
            }
            e.inferred_type = to;
            return;
        }
        case parse::Kind::kStringifyType: {
            // ##type(expr): infer the operand's type, then BECOME a string
            // literal holding that type name. An alias/enum-labeled operand
            // reports its label (the as-declared name); everything else reports
            // its erased underlying type. Lowered in place here so every
            // downstream stage only ever sees a kStringLiteral.
            assert(e.children.size() == 1 && "kStringifyType needs 1 operand");
            // Type-name operand: resolve stamped the underlying type on return_type
            // (`##type(Integer)` -> `int`). Emit it directly; the operand is a type,
            // not a value, so skip inferExpr.
            if (!e.return_type.empty()) {
                e.text = e.return_type;
                e.return_type.clear();
                e.children.clear();
                e.kind = parse::Kind::kStringLiteral;
                e.inferred_type = "char[]";
                return;
            }
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, "", diag);
            // A DIRECT reference to a const reports its const-qualified declared
            // type (alias label if it had one). Reading a const inside any larger
            // expression strips const — the simplified no-pointer rule — so only a
            // bare const ident takes the qualifier; everything else reports its
            // (already const-free) inferred type or alias label.
            if (operand.kind == parse::Kind::kIdentExpr
                && operand.resolved_entry_id >= 0
                && tree.entries[operand.resolved_entry_id].kind
                       == parse::EntryKind::kConst) {
                parse::Entry const& ce = tree.entries[operand.resolved_entry_id];
                e.text = "const "
                    + (ce.alias_label.empty() ? ce.slids_type : ce.alias_label);
            } else if (!operand.alias_label.empty()) {
                e.text = operand.alias_label;
            } else if (isLiteralKind(operand.kind)) {
                // A bare literal reports the no-width preferred spelling
                // (int/uint/float), consistent with const-capture and the
                // no-width preference — a value of a DECLARED width keeps its
                // width name (the else below). char has no width-name to fold.
                e.text = preferredSpelling(operand.inferred_type);
            } else {
                e.text = operand.inferred_type;
            }
            e.children.clear();
            e.kind = parse::Kind::kStringLiteral;
            e.inferred_type = "char[]";
            return;
        }
        case parse::Kind::kIdentExpr: {
            assert(e.resolved_entry_id >= 0
                && "inferExpr kIdentExpr: resolve did not stamp resolved_entry_id");
            e.inferred_type = parse::entryType(tree, e.resolved_entry_id);
            e.alias_label = tree.entries[e.resolved_entry_id].alias_label;
            return;
        }
        case parse::Kind::kCallExpr: {
            // Pick the overload (if any) + infer args; then the call yields its
            // return type (widening into `context` happens at codegen, like an
            // ident read). Reject a void return used where a value is wanted.
            classifyCall(tree, e, diag);
            if (e.return_type == "void") {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Function '" + e.name + "' returns no value and cannot be "
                    "used as an expression.", {}});
            }
            e.inferred_type = e.return_type;
            return;
        }
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr: {
            // An inc/dec yields its operand's type. Phase 1: int-class (not
            // bool) and float scalars step by 1; pointers (element stride) and
            // everything else are rejected until their phases wire the step.
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, "", diag);
            std::string const& ot = operand.inferred_type;
            if (isReference(ot)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Arithmetic is not allowed on a reference.", {}});
            } else if (isIteratorType(ot)) {
                // ok — an iterator steps by one element.
            } else if (!ot.empty() && (!isNumericType(ot) || ot == "bool")) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Operator '" + e.text + "' is not defined on type '"
                    + ot + "'.", {}});
            }
            e.inferred_type = ot;
            return;
        }
        case parse::Kind::kUnaryExpr: {
            assert(e.children.size() == 1 && "UnaryExpr needs 1 child");
            parse::Node& operand = *e.children[0];
            std::string const& op = e.text;
            if (op == "!") {
                inferExpr(tree, operand, "", diag);
                if (!operand.inferred_type.empty()
                    && !isCoercibleToBool(operand.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '!' is not defined on type '"
                        + operand.inferred_type + "'.", {}});
                }
                e.inferred_type = "bool";
            } else {  // + - ~
                inferExpr(tree, operand, context, diag);
                e.inferred_type = operand.inferred_type;
                e.alias_label = operand.alias_label;   // a unary keeps the label
            }
            return;
        }
        case parse::Kind::kBinaryExpr: {
            assert(e.children.size() == 2 && "BinaryExpr needs 2 children");
            parse::Node& lhs = *e.children[0];
            parse::Node& rhs = *e.children[1];
            std::string const& op = e.text;

            if (op == "&&" || op == "||" || op == "^^") {
                inferExpr(tree, lhs, "", diag);
                inferExpr(tree, rhs, "", diag);
                if (!lhs.inferred_type.empty()
                    && !isCoercibleToBool(lhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + lhs.inferred_type + "'.", {}});
                }
                if (!rhs.inferred_type.empty()
                    && !isCoercibleToBool(rhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + rhs.inferred_type + "'.", {}});
                }
                e.inferred_type = "bool";
                e.op_type = "bool";
                return;
            }

            if (op == "<<" || op == ">>") {
                inferExpr(tree, lhs, context, diag);
                // Shift count stands alone — flexing into a float lhs would
                // mis-type a small int literal. Codegen handles width mismatch.
                inferExpr(tree, rhs, "", diag);
                if (!lhs.inferred_type.empty()
                    && !isNumericType(lhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Shift left-hand side must be numeric; got '"
                        + lhs.inferred_type + "'.", {}});
                }
                if (!rhs.inferred_type.empty()
                    && !isIntegerClass(rhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Shift count must be integer-class; got '"
                        + rhs.inferred_type + "'.", {}});
                }
                e.inferred_type = lhs.inferred_type;
                e.op_type = lhs.inferred_type;
                e.alias_label = lhs.alias_label;   // a shift keeps the lhs label
                return;
            }

            // Comparison and arith/bitwise: infer each side without context,
            // then literal-flex, then commonType.
            inferExpr(tree, lhs, "", diag);
            inferExpr(tree, rhs, "", diag);

            // Pointer operands (reference / iterator / nullptr): the six
            // relational ops compare them (same pointee type required; nullptr
            // exempt), and arithmetic is rejected. Handled before the numeric
            // commonType path, which has no notion of pointer types.
            if (isPtrLikeType(lhs.inferred_type)
                || isPtrLikeType(rhs.inferred_type)) {
                bool is_cmp = (op == "==" || op == "!=" || op == "<"
                            || op == "<=" || op == ">" || op == ">=");
                if (!is_cmp) {
                    // References admit no arithmetic at all.
                    if (isReference(lhs.inferred_type)
                        || isReference(rhs.inferred_type)) {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "Arithmetic is not allowed on a reference.", {}});
                        return;
                    }
                    // Iterators step by element: `iter ± int` -> iterator;
                    // `iter - iter` (same pointee) -> intptr (element count).
                    bool lit = isIteratorType(lhs.inferred_type);
                    bool rit = isIteratorType(rhs.inferred_type);
                    if (op == "+"
                        && ((lit && isIntegerClass(rhs.inferred_type))
                            || (rit && isIntegerClass(lhs.inferred_type)))) {
                        e.inferred_type = lit ? lhs.inferred_type
                                              : rhs.inferred_type;
                        e.op_type = e.inferred_type;
                        return;
                    }
                    if (op == "-" && lit && isIntegerClass(rhs.inferred_type)) {
                        e.inferred_type = lhs.inferred_type;
                        e.op_type = lhs.inferred_type;
                        return;
                    }
                    if (op == "-" && lit && rit) {
                        if (pointeeType(lhs.inferred_type)
                                != pointeeType(rhs.inferred_type)) {
                            diagnostic::report(diag, {e.file_id, e.tok,
                                "Pointer subtraction requires the same pointee "
                                "type.", {}});
                            return;
                        }
                        e.inferred_type = "intptr";
                        e.op_type = lhs.inferred_type;   // element stride for codegen
                        return;
                    }
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Arithmetic is not defined on a pointer.", {}});
                    return;
                }
                // A reference has no sequence position: only `==` / `!=` apply.
                // Ordering is reserved for iterators (and iterator/reference
                // pairs degrade to `==` / `!=` when a reference is involved).
                bool ordering = (op == "<" || op == "<=" || op == ">"
                              || op == ">=");
                if (ordering && (isReference(lhs.inferred_type)
                                 || isReference(rhs.inferred_type))) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "References support only '==' and '!=' comparison.",
                        {}});
                    return;
                }
                bool lnull = lhs.inferred_type == "anyptr";
                bool rnull = rhs.inferred_type == "anyptr";
                if (!lnull && !rnull
                    && pointeeType(lhs.inferred_type)
                           != pointeeType(rhs.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Pointer comparison requires the same pointee type.",
                        {}});
                    return;
                }
                e.inferred_type = "bool";
                e.op_type = lnull ? rhs.inferred_type : lhs.inferred_type;
                return;
            }

            flexBinaryOperands(lhs, rhs);

            std::string opty;
            bool ok = widen::commonType(lhs.inferred_type, rhs.inferred_type, opty);
            if (!ok) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "No common type for '" + lhs.inferred_type + "' and '"
                    + rhs.inferred_type + "'; use an explicit type conversion.",
                    {}});
                return;
            }

            if ((op == "&" || op == "|" || op == "^") && isFloatType(opty)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Bitwise '" + op + "' not defined on floating-point type '"
                    + opty + "'.", {}});
                return;
            }

            bool is_cmp = (op == "==" || op == "!=" || op == "<"
                        || op == "<=" || op == ">"  || op == ">=");
            e.inferred_type = is_cmp ? std::string("bool") : opty;
            e.op_type = opty;
            // A comparison yields bool (no label); an arith/bitwise result keeps
            // an alias label only when both sides agree (or a literal flexes in).
            if (!is_cmp) e.alias_label = binaryLabel(lhs, rhs);
            return;
        }
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kStoreStmt:
        case parse::Kind::kDeleteStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kExprStmt:
        case parse::Kind::kAliasDecl:
        case parse::Kind::kNamespaceDecl:
        case parse::Kind::kEnumDecl:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kBlockStmt:
        case parse::Kind::kIfStmt:
        case parse::Kind::kWhileStmt:
        case parse::Kind::kDoWhileStmt:
        case parse::Kind::kForLongStmt:
        case parse::Kind::kForEnumStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kSwitchStmt:
        case parse::Kind::kCaseClause:
        case parse::Kind::kParam:
            assert(false && "inferExpr: not an expression kind");
            return;
    }
}

// Append literal arg nodes for omitted trailing OPTIONAL params, from the
// function entry's captured constant defaults.
void fillDefaults(parse::Node& s, parse::Entry const& e) {
    for (std::size_t i = s.children.size();
         i < e.param_types.size() && i < e.param_default_kind.size(); i++) {
        auto lit = std::make_unique<parse::Node>();
        lit->kind = e.param_default_kind[i];
        lit->text = e.param_default_text[i];
        lit->file_id = s.file_id;
        lit->tok = s.tok;
        s.children.push_back(std::move(lit));
    }
}

void classifyCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    // Gather same-name free-function candidates (an unqualified call). A
    // qualified call was resolved to a single namespace member upstream; a single
    // free function keeps resolve's resolution.
    std::vector<int> cands;
    bool qualified = !s.qualifier.empty() || s.global_qualified;
    if (!qualified) {
        for (std::size_t id = 0; id < tree.entries.size(); id++) {
            parse::Entry const& e = tree.entries[id];
            // Overload candidates are FILE-SCOPE free functions (parent_frame_id
            // == the global frame 0). A nested function (in a body frame) was
            // already resolved singly by resolve and must not collide with a
            // same-named nested function in another host.
            if (e.kind == parse::EntryKind::kFunction && e.owner_ns_frame < 0
                && e.parent_frame_id == 0 && e.name == s.name) {
                cands.push_back((int)id);
            }
        }
    }
    if (cands.size() <= 1) {
        // single candidate (or qualified): refresh the signature from the entry
        // (a typeless param is now typed), fill omitted optional args, then infer.
        if (s.resolved_entry_id >= 0) {
            parse::Entry const& e = tree.entries[s.resolved_entry_id];
            s.return_type = e.slids_type;
            s.param_types = e.param_types;
            s.captures = e.captures;       // a nested-fn call passes these
            fillDefaults(s, e);
        }
        for (std::size_t i = 0; i < s.children.size(); i++) {
            if (!s.children[i]) continue;
            std::string const& dest = (i < s.param_types.size())
                ? s.param_types[i] : std::string();
            inferExpr(tree, *s.children[i], dest, diag);
            checkStrongConstAssign(dest, *s.children[i], diag);
        }
        return;
    }
    // 2+ overloads: infer the PROVIDED args without context, then rank by cost
    // (a candidate is viable if its arity range admits this arg count and every
    // provided arg converts; omitted trailing optionals are filled from defaults).
    for (auto& ch : s.children) {
        if (ch) inferExpr(tree, *ch, "", diag);
    }
    int best = -1, best_cost = INT_MAX;
    bool tie = false;
    for (int cid : cands) {
        parse::Entry const& e = tree.entries[cid];
        if (s.children.size() < (std::size_t)e.num_required
            || s.children.size() > e.param_types.size()) {
            continue;   // arity range
        }
        int cost = 0;
        bool ok = true;
        for (std::size_t i = 0; i < s.children.size() && ok; i++) {
            int c = s.children[i] ? argConvertCost(*s.children[i], e.param_types[i])
                                  : -1;
            if (c < 0) ok = false; else cost += c;
        }
        if (!ok) continue;
        if (cost < best_cost) { best_cost = cost; best = cid; tie = false; }
        else if (cost == best_cost) { tie = true; }
    }
    if (best < 0) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "No matching overload for '" + s.name + "'.", {}});
        return;
    }
    if (tie) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Ambiguous call to '" + s.name + "'; multiple overloads match.", {}});
        return;
    }
    s.resolved_entry_id = best;
    s.return_type = tree.entries[best].slids_type;
    s.param_types = tree.entries[best].param_types;
    s.captures = tree.entries[best].captures;
    fillDefaults(s, tree.entries[best]);
    // Re-infer all args (incl. filled defaults) with the chosen param context;
    // a strong-const literal arg obeys the typed-value widen rules into the param.
    for (std::size_t i = 0; i < s.children.size(); i++) {
        if (!s.children[i]) continue;
        inferExpr(tree, *s.children[i], s.param_types[i], diag);
        if (i < s.param_types.size())
            checkStrongConstAssign(s.param_types[i], *s.children[i], diag);
    }
}

// A condition whose value is known at compile time (constfold ran upstream, so a
// constant condition is a folded literal — incl. a substituted const or the
// synthesized empty-`()` true). Drives constant-branch unreachable detection.
enum class CondConst { True, False, NotConst };

bool literalIsZero(parse::Node const& n) {
    if (n.kind == parse::Kind::kFloatLiteral) {
        return std::strtod(n.text.c_str(), nullptr) == 0.0;   // ±0.0
    }
    // bool ("1"/"0"), int / uint / char: decimal magnitude (numeric-canonical).
    errno = 0;
    char* end = nullptr;
    long long v = std::strtoll(n.text.c_str(), &end, 10);
    if (errno == ERANGE) return false;   // doesn't fit -> certainly nonzero
    return v == 0;
}

CondConst constTruth(parse::Node const& cond) {
    if (cond.kind == parse::Kind::kBoolLiteral
        || cond.kind == parse::Kind::kIntLiteral
        || cond.kind == parse::Kind::kUintLiteral
        || cond.kind == parse::Kind::kCharLiteral
        || cond.kind == parse::Kind::kFloatLiteral) {
        return literalIsZero(cond) ? CondConst::False : CondConst::True;
    }
    return CondConst::NotConst;
}

// Report the first statement of an unreachable branch. The branch is a kBlockStmt
// (then / else / loop body) or, for an `else if`, a nested kIfStmt. An empty
// block has nothing to flag.
void reportUnreachableBranch(parse::Node const& branch, diagnostic::Sink& diag) {
    if (branch.kind == parse::Kind::kBlockStmt) {
        for (auto const& ch : branch.children) {
            if (ch) {
                diagnostic::report(diag, {ch->file_id, ch->tok,
                    "Unreachable statement.", {}});
                return;
            }
        }
        return;   // empty block: nothing unreachable
    }
    diagnostic::report(diag, {branch.file_id, branch.tok,
        "Unreachable statement.", {}});   // else-if chain
}

// `a cmp b` for the ranged-for empty-range check. Unknown cmp -> true (don't flag).
template <typename T>
bool applyCmp(T a, T b, std::string const& cmp) {
    if (cmp == "<")  return a < b;
    if (cmp == "<=") return a <= b;
    if (cmp == ">")  return a > b;
    if (cmp == ">=") return a >= b;
    if (cmp == "!=") return a != b;
    return true;
}

// Empty-range check for a ranged-for: both bounds constant (folded literals) and
// `start cmp end` FALSE means the loop body can never run. Non-literal bounds ->
// not decidable -> not an error.
bool rangeFirstTestFalse(parse::Node const& start, parse::Node const& end,
                         std::string const& cmp) {
    auto isLit = [](parse::Node const& n) {
        return n.kind == parse::Kind::kBoolLiteral
            || n.kind == parse::Kind::kIntLiteral
            || n.kind == parse::Kind::kUintLiteral
            || n.kind == parse::Kind::kCharLiteral
            || n.kind == parse::Kind::kFloatLiteral;
    };
    if (!isLit(start) || !isLit(end)) return false;
    if (start.kind == parse::Kind::kFloatLiteral
        || end.kind == parse::Kind::kFloatLiteral) {
        double a = std::strtod(start.text.c_str(), nullptr);
        double b = std::strtod(end.text.c_str(), nullptr);
        return !applyCmp(a, b, cmp);
    }
    errno = 0;
    long long a = std::strtoll(start.text.c_str(), nullptr, 10);
    if (errno == ERANGE) return false;
    errno = 0;
    long long b = std::strtoll(end.text.c_str(), nullptr, 10);
    if (errno == ERANGE) return false;
    return !applyCmp(a, b, cmp);
}

// Enforce the implicit pointer-cast rules at an assignment to a variable
// (`lvalue = rhs`, or a typed var-decl init). Only fires when a pointer is
// involved on either side — pure-numeric assignments keep the width-coercion
// path. An info-adding implicit cast is rejected here; the user must write an
// explicit `<Type^>` (which then passes this check as a same-typed rhs).
void checkPtrAssign(std::string const& lvalue_type, parse::Node const& rhs,
                    diagnostic::Sink& diag) {
    std::string const& R = rhs.inferred_type;
    // An empty type on either side rides an already-reported upstream error
    // (an unresolved name, a void value, ...) — skip silently rather than emit a
    // misleading second diagnostic. Deliberate, not a missing check.
    if (R.empty() || lvalue_type.empty()) return;
    if (!isPtrLikeType(lvalue_type) && !isPtrLikeType(R)) return;  // numeric path
    if (ptrImplicitOk(R, lvalue_type)) return;
    diagnostic::report(diag, {rhs.file_id, rhs.tok,
        "Cannot implicitly cast '" + R + "' to '" + lvalue_type
        + "'; an explicit cast is required.", {}});
}

// A strong-const LITERAL rhs is a TYPED value, not a flexing literal: assigning it
// to `dest` must be a same-type or widen-within-family conversion, exactly as a
// variable of the strong type would be — `const int sc=5; int8 a=sc;` narrows and
// is rejected. codegen's literal path only RANGE-checks (so it misses the
// narrowing); this mirrors widen::convert's accept/reject + wording for the
// same-/cross-sign integer and float cases. CROSS-FAMILY (int<->float) and a bool
// source are left to codegen's convert (so they aren't double-reported). A weak
// literal (no strong_type) still flexes; the const-fold demotion guarantees a
// strong literal's value always fits its strong type, so no value check is needed.
void checkStrongConstAssign(std::string const& dest, parse::Node const& rhs,
                            diagnostic::Sink& diag) {
    if (rhs.strong_type.empty() || !isLiteralKind(rhs.kind) || dest.empty()) return;
    std::string const& src = rhs.strong_type;
    widen::TypeKind st, dt;
    if (!widen::classify(src, st) || !widen::classify(dest, dt)) return;
    if (st.cat == dt.cat && st.bits == dt.bits) return;          // same type
    using C = widen::Category;
    auto narrow = [&] {
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Cannot implicitly narrow '" + src + "' to '" + dest
            + "'; use an explicit type conversion.", {}});
    };
    auto convertErr = [&](char const* tail) {
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Cannot implicitly convert '" + src + "' to '" + dest + "' (" + tail
            + "); use an explicit type conversion.", {}});
    };
    if ((st.cat == C::kSignedInt   && dt.cat == C::kSignedInt)
     || (st.cat == C::kUnsignedInt && dt.cat == C::kUnsignedInt)
     || (st.cat == C::kFloat       && dt.cat == C::kFloat)) {
        if (dt.bits <= st.bits) narrow();                        // same sign: narrowing
    } else if (st.cat == C::kUnsignedInt && dt.cat == C::kSignedInt) {
        if (dt.bits <= st.bits) convertErr("unsigned to same-width signed");
    } else if (st.cat == C::kSignedInt && dt.cat == C::kUnsignedInt) {
        convertErr("signed \xe2\x86\x92 unsigned");
    }
    // bool source / int<->float cross-family: left to codegen's convert.
}

void classifyStmt(parse::Tree& tree, parse::Node& s,
                  std::string const& fn_return_type,
                  diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            if (!s.children.empty()) {
                inferExpr(tree, *s.children[0], s.return_type, diag);
                // Inferred-init: a typeless decl (empty return_type, promoted from
                // an assign in resolve) takes the rhs type. A literal-inferred type
                // normalizes to its preferred spelling (int32->int, ...); a typed
                // rhs keeps its spelling. Copy any alias label, then WRITE BACK the
                // entry — the one deliberate symbol-table mutation in classify, so
                // later reads resolve to the inferred type.
                // Typeless CONSTS are inferred by constfold (it folds + captures
                // and stamps slids_type with strong/weak); don't clobber that here.
                if (s.return_type.empty() && s.resolved_entry_id >= 0
                    && !s.is_const) {
                    parse::Node& rhs = *s.children[0];
                    // Invariant: a typeable rhs yields a non-empty inferred type.
                    // An un-typeable one (namespace/type-as-value, no-common-type,
                    // void call) reports a diagnostic first and main short-circuits
                    // before codegen — so an empty type here only ever coexists
                    // with an already-reported error (don't abort on that path).
                    assert((diagnostic::hasErrors(diag)
                            || !rhs.inferred_type.empty())
                        && "inferred-init: a typeable rhs must yield a type");
                    std::string t = isLiteralKind(rhs.kind)
                        ? preferredSpelling(rhs.inferred_type)
                        : rhs.inferred_type;
                    s.return_type = t;
                    tree.entries[s.resolved_entry_id].slids_type = t;
                    tree.entries[s.resolved_entry_id].alias_label = rhs.alias_label;
                }
                // A typed pointer init obeys the implicit-cast rules (a typeless
                // init took the rhs type above, so there is nothing to cast); a
                // strong-const literal init obeys the typed-value widen rules.
                if (!s.return_type.empty()) {
                    checkPtrAssign(s.return_type, *s.children[0], diag);
                    checkStrongConstAssign(s.return_type, *s.children[0], diag);
                }
            }
            return;
        }
        case parse::Kind::kAssignStmt: {
            // resolve already stamped resolved_entry_id (or emitted an error
            // and we won't reach here — main short-circuits on resolve errors).
            std::string lvalue_type;
            if (s.resolved_entry_id >= 0) {
                lvalue_type = parse::entryType(tree, s.resolved_entry_id);
            }
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, lvalue_type, diag);
            }
            // Assigning to a pointer variable obeys the implicit-cast rules; a
            // strong-const literal rhs obeys the typed-value widen rules.
            if (!s.children.empty() && s.children[0]) {
                checkPtrAssign(lvalue_type, *s.children[0], diag);
                checkStrongConstAssign(lvalue_type, *s.children[0], diag);
            }
            return;
        }
        case parse::Kind::kAugAssignStmt: {
            // resolve stamped resolved_entry_id and cached lvalue type as
            // s.return_type. If resolve failed, we shouldn't be here (main
            // short-circuits) — but be defensive and walk rhs anyway.
            assert(s.children.size() == 1 && "AugAssignStmt needs 1 rhs child");
            parse::Node& rhs = *s.children[0];
            std::string const& op = s.text;
            // Refresh the lvalue type from the entry: an inferred-init local was
            // still untyped when resolve cached s.return_type here, but classify
            // stamped it when it ran the (promoted) decl above. desugar reads
            // s.return_type, so update it in place. For an ordinary local this is
            // the same value resolve already cached.
            if (s.resolved_entry_id >= 0) {
                s.return_type = parse::entryType(tree, s.resolved_entry_id);
            }
            std::string const& lvalue_type = s.return_type;

            // A reference admits no compound arithmetic/bitwise assignment.
            if (isReference(lvalue_type)) {
                inferExpr(tree, rhs, "", diag);
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Arithmetic is not allowed on a reference.", {}});
                return;
            }

            // An iterator steps by element: `iter += n` / `iter -= n` only (the
            // compound form of `iter ± int`). Any other compound op on a pointer
            // is rejected like the binary path. Desugar lowers this to
            // `iter = iter + n`, whose GEP + assignment already handle it.
            if (isIteratorType(lvalue_type)) {
                inferExpr(tree, rhs, "", diag);
                if ((op == "+" || op == "-")
                    && isIntegerClass(rhs.inferred_type)) {
                    s.inferred_type = lvalue_type;
                    s.op_type = lvalue_type;
                    return;
                }
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Arithmetic is not defined on a pointer.", {}});
                return;
            }

            if (op == "<<" || op == ">>") {
                inferExpr(tree, rhs, "", diag);
                if (!isNumericType(lvalue_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Shift left-hand side must be numeric; got '"
                        + lvalue_type + "'.", {}});
                }
                if (!rhs.inferred_type.empty()
                    && !isIntegerClass(rhs.inferred_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Shift count must be integer-class; got '"
                        + rhs.inferred_type + "'.", {}});
                }
                s.inferred_type = lvalue_type;
                s.op_type = lvalue_type;
                return;
            }
            if (op == "&&" || op == "||" || op == "^^") {
                inferExpr(tree, rhs, "", diag);
                if (!isCoercibleToBool(lvalue_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + lvalue_type + "'.", {}});
                }
                if (!rhs.inferred_type.empty()
                    && !isCoercibleToBool(rhs.inferred_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + rhs.inferred_type + "'.", {}});
                }
                s.inferred_type = "bool";
                s.op_type = "bool";
                return;
            }
            // arith / bitwise — rhs literal flexes into lvalue's type, then
            // commonType drives the op.
            inferExpr(tree, rhs, lvalue_type, diag);
            std::string opty;
            if (!widen::commonType(lvalue_type, rhs.inferred_type, opty)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "No common type for '" + lvalue_type + "' and '"
                    + rhs.inferred_type
                    + "'; use an explicit type conversion.", {}});
                return;
            }
            if ((op == "&" || op == "|" || op == "^") && isFloatType(opty)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Bitwise '" + op + "' not defined on floating-point type '"
                    + opty + "'.", {}});
                return;
            }
            s.inferred_type = opty;
            s.op_type = opty;
            return;
        }
        case parse::Kind::kStoreStmt: {
            // `lvalue^ = rhs` (deref) or `arr[i] = rhs` (index). Infer the lvalue
            // (-> the pointee / element type), then infer the rhs in that context
            // so a literal flexes to it.
            assert(s.children.size() == 2 && "kStoreStmt needs lvalue + rhs");
            parse::Node& lvalue = *s.children[0];
            inferExpr(tree, lvalue, "", diag);
            inferExpr(tree, *s.children[1], lvalue.inferred_type, diag);
            // A store into a pointer-typed slot (an element of a references array,
            // or a deref whose pointee is itself a pointer) obeys the same
            // implicit-cast rules as a plain assignment — storing an unrelated
            // pointer here would otherwise reach codegen and emit invalid IR. A
            // strong-const literal stored here obeys the typed-value widen rules.
            checkPtrAssign(lvalue.inferred_type, *s.children[1], diag);
            checkStrongConstAssign(lvalue.inferred_type, *s.children[1], diag);
            return;
        }
        case parse::Kind::kDeleteStmt: {
            // delete p; — p must be a pointer (reference / iterator). resolve
            // already checked it is a variable lvalue.
            parse::Node& operand = *s.children[0];
            inferExpr(tree, operand, "", diag);
            if (!operand.inferred_type.empty()
                && !isPtrLikeType(operand.inferred_type)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Cannot delete a non-pointer value of type '"
                    + operand.inferred_type + "'.", {}});
            }
            return;
        }
        case parse::Kind::kCallStmt: {
            if (isPrintIntrinsic(s.name)) {
                for (auto& ch : s.children) {
                    if (ch) inferPrintArg(tree, *ch, diag);
                }
                return;
            }
            // Pick the overload (if any) and infer each arg with the chosen
            // param type as context.
            classifyCall(tree, s, diag);
            return;
        }
        case parse::Kind::kReturnStmt: {
            // A void function returning a value would emit `ret void <val>`
            // (invalid IR). Reject at the `return`. (Non-literal values would
            // otherwise slip past the literal-fit check into codegen.)
            if (fn_return_type == "void" && !s.children.empty()) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A void function cannot return a value.", {}});
                return;
            }
            // A bare `return;` (no value) is only valid in a void function.
            if (fn_return_type != "void" && s.children.empty()) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A non-void function must return a value.", {}});
                return;
            }
            for (auto& ch : s.children) {
                if (ch) {
                    inferExpr(tree, *ch, fn_return_type, diag);
                    checkStrongConstAssign(fn_return_type, *ch, diag);
                }
            }
            return;
        }
        case parse::Kind::kExprStmt: {
            // value discarded — infer for its checks (lvalue / numeric) only.
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, "", diag);
            }
            return;
        }
        case parse::Kind::kAliasDecl:
            // resolve substituted the alias away; nothing to type here.
            return;
        case parse::Kind::kNamespaceDecl:
            classifyNamespace(tree, s, diag);
            return;
        case parse::Kind::kEnumDecl:
            // Enum members were lowered to kConst entries at resolve and folded
            // by constfold; the enum node carries nothing to type-infer.
            return;
        case parse::Kind::kBlockStmt:
            // A nested scope: type-infer each contained statement.
            for (auto& ch : s.children) {
                if (ch) classifyStmt(tree, *ch, fn_return_type, diag);
            }
            return;
        case parse::Kind::kIfStmt: {
            // children[0] = condition, [1] = then-branch, [2] = optional else.
            // The condition truthy-coerces (same rule as `!`/`&&`/`||`); a void
            // or other non-value-typed condition is rejected.
            assert(s.children.size() >= 2 && "kIfStmt needs condition + then");
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, "", diag);
            if (!cond.inferred_type.empty()
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "An if condition must be a condition expression; type '"
                    + cond.inferred_type + "' is not.", {}});
            }
            // Constant condition -> the opposite branch is dead: a const-true if
            // never enters the else, a const-false if never enters the then.
            bool has_else = s.children.size() > 2 && s.children[2];
            CondConst c = constTruth(cond);
            if (c == CondConst::True && has_else) {
                reportUnreachableBranch(*s.children[2], diag);
            } else if (c == CondConst::False) {
                reportUnreachableBranch(*s.children[1], diag);
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);
            if (has_else) {
                classifyStmt(tree, *s.children[2], fn_return_type, diag);
            }
            return;
        }
        case parse::Kind::kWhileStmt: {
            // children[0] = condition, [1] = body. Condition truthy-coerces
            // (same rule as the if condition).
            assert(s.children.size() == 2 && "kWhileStmt needs condition + body");
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, "", diag);
            if (!cond.inferred_type.empty()
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A while condition must be a condition expression; type '"
                    + cond.inferred_type + "' is not.", {}});
            }
            // A constant-false pre-condition never runs the body. A constant-true
            // loop is NOT flagged: per 3B there is no constant-true loop special
            // case (the body is reachable; an unreachable after-loop is deferred).
            if (constTruth(cond) == CondConst::False) {
                reportUnreachableBranch(*s.children[1], diag);
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);
            return;
        }
        case parse::Kind::kDoWhileStmt: {
            // children[0] = condition, [1] = body. Same shape as kWhileStmt; the
            // condition truthy-coerces. (Flow ordering — body-then-test — is a
            // resolve/codegen concern; type inference is order-independent.)
            assert(s.children.size() == 2 && "kDoWhileStmt needs condition + body");
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, "", diag);
            if (!cond.inferred_type.empty()
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A while condition must be a condition expression; type '"
                    + cond.inferred_type + "' is not.", {}});
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);
            return;
        }
        case parse::Kind::kForLongStmt: {
            // children[0]=cond, [1]=update, [2]=body, [3..]=varlist decls.
            assert(s.children.size() >= 3 && "kForLongStmt needs cond+update+body");
            // The loop var [3] is classified first so a typeless (inferred) loop
            // var has its type stamped before the rest of the varlist. For a
            // ranged/enum for, the synthesized `_$end`/`_$step` (children[4..])
            // share the loop var's type per the desugar spec — when they are
            // typeless (the loop var was inferred), give them the loop var's
            // resolved type as context so their bounds flex into it, matching an
            // explicitly-typed range (where parse already stamped the type).
            if (s.children.size() > 3 && s.children[3]) {
                classifyStmt(tree, *s.children[3], fn_return_type, diag);
            }
            if (s.range_dotdot_tok >= 0 && s.children.size() > 3 && s.children[3]
                && s.children[3]->resolved_entry_id >= 0) {
                std::string lv = parse::entryType(tree, s.children[3]->resolved_entry_id);
                for (std::size_t i = 4; i < s.children.size(); i++) {
                    if (s.children[i]
                        && s.children[i]->kind == parse::Kind::kVarDeclStmt
                        && s.children[i]->return_type.empty()
                        && s.children[i]->resolved_entry_id >= 0
                        && !lv.empty()) {
                        s.children[i]->return_type = lv;
                        tree.entries[s.children[i]->resolved_entry_id].slids_type = lv;
                    }
                }
            }
            for (std::size_t i = 4; i < s.children.size(); i++) {
                if (s.children[i]) classifyStmt(tree, *s.children[i], fn_return_type, diag);
            }
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, "", diag);
            if (!cond.inferred_type.empty()
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A for condition must be a condition expression; type '"
                    + cond.inferred_type + "' is not.", {}});
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);   // update
            classifyStmt(tree, *s.children[2], fn_return_type, diag);   // body
            // Ranged-for empty-range check: children[3] = loop var (init = start),
            // [4] = _$end (init = end); cond.text = cmp. Both bounds constant and
            // `start cmp end` false -> the body never runs -> "Invalid range." at
            // the `..`. (Ranged-for only — gated on range_dotdot_tok.)
            if (s.range_dotdot_tok >= 0 && s.children.size() >= 5
                && !s.children[3]->children.empty()
                && !s.children[4]->children.empty()
                && rangeFirstTestFalse(*s.children[3]->children[0],
                                       *s.children[4]->children[0], cond.text)) {
                diagnostic::report(diag, {s.file_id, s.range_dotdot_tok,
                    "Invalid range.", {}});
            }
            return;
        }
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
            // Nothing to type-infer; resolve handled loop-legality.
            return;
        case parse::Kind::kSwitchStmt: {
            // children[0] = scrutinee, [1..] = kCaseClause. The scrutinee must be
            // integer-class; each case label must be a unique integer constant
            // (constfold folded them to literals); default is singular.
            assert(!s.children.empty() && "kSwitchStmt needs a scrutinee");
            parse::Node& scrut = *s.children[0];
            inferExpr(tree, scrut, "", diag);
            std::string const& st = scrut.inferred_type;
            if (!st.empty() && !isIntegerClass(st)) {
                diagnostic::report(diag, {scrut.file_id, scrut.tok,
                    "A switch value must be integer-class; got '" + st + "'.",
                    {}});
            }
            std::map<long long, parse::Node const*> seen;   // value -> first label
            int default_tok = -1;
            for (std::size_t i = 1; i < s.children.size(); i++) {
                parse::Node& clause = *s.children[i];
                if (clause.children[0]) {
                    parse::Node& label = *clause.children[0];
                    inferExpr(tree, label, st, diag);
                    if (!isLiteralKind(label.kind)) {
                        diagnostic::report(diag, {label.file_id, label.tok,
                            "A case label must be a constant.", {}});
                    } else if (label.kind == parse::Kind::kFloatLiteral
                               || (!label.inferred_type.empty()
                                   && !isIntegerClass(label.inferred_type))) {
                        diagnostic::report(diag, {label.file_id, label.tok,
                            "A case label must be an integer constant.", {}});
                    } else if (!st.empty() && !literalFitsContext(label, st)) {
                        // An out-of-range / sign-mismatched label can never match
                        // and would emit a truncated `iN <oob>` constant — reject
                        // it here rather than emit invalid/misleading IR.
                        diagnostic::report(diag, {label.file_id, label.tok,
                            "Case label '" + label.text
                                + "' is out of range for switch type '" + st
                                + "'.", {}});
                    } else {
                        // Dedup by numeric value (so 'a' and 97 collide). Parse
                        // the full 64-bit range: a uint64 above INT64_MAX is read
                        // via strtoull and reinterpreted, so distinct values stay
                        // distinct (no false duplicate from signed overflow).
                        errno = 0;
                        long long v = std::strtoll(label.text.c_str(), nullptr, 10);
                        if (errno == ERANGE) {
                            v = static_cast<long long>(
                                std::strtoull(label.text.c_str(), nullptr, 10));
                        }
                        auto ins = seen.emplace(v, &label);
                        if (!ins.second) {
                            parse::Node const* first = ins.first->second;
                            diagnostic::report(diag, {label.file_id, label.tok,
                                "Duplicate case label '" + label.text + "'.",
                                {{first->file_id, first->tok, "first case here"}}});
                        }
                    }
                } else {
                    if (default_tok >= 0) {
                        diagnostic::report(diag, {clause.file_id, clause.tok,
                            "A switch may have only one default clause.",
                            {{s.children[0]->file_id, default_tok,
                              "first default here"}}});
                    }
                    default_tok = clause.tok;
                }
                classifyStmt(tree, *clause.children[1], fn_return_type, diag);
            }
            return;
        }
        case parse::Kind::kCaseClause:
            // Handled inline by the kSwitchStmt arm above; never classified alone.
            assert(false && "classifyStmt: kCaseClause outside a switch");
            return;
        case parse::Kind::kForEnumStmt:
            // Lowered to a kForLongStmt during resolve; never reaches classify.
            assert(false && "classifyStmt: kForEnumStmt survived resolve");
            return;
        case parse::Kind::kFunctionDef:
            // A nested function: type-check its body + return-correctness, then
            // record each capture's (now-finalized) type for codegen lifting.
            classifyFunctionBody(tree, s, diag);
            s.capture_types.clear();
            for (int cid : s.captures) {
                s.capture_types.push_back(parse::entryType(tree, cid));
            }
            return;
        case parse::Kind::kFunctionDecl:
            // A nested forward declaration carries no body to type-check.
            return;
        case parse::Kind::kProgram:
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral:
        case parse::Kind::kNullptrLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr:
        case parse::Kind::kAddrOfExpr:
        case parse::Kind::kDerefExpr:
        case parse::Kind::kIndexExpr:
        case parse::Kind::kCastExpr:
        case parse::Kind::kNewExpr:
        case parse::Kind::kSizeofExpr:
        case parse::Kind::kStringifyType:
        case parse::Kind::kCallExpr:
        case parse::Kind::kParam:
            assert(false && "classifyStmt: not a statement kind");
            return;
    }
}

// "Last statement completes by returning" — the trailing-return heuristic. A
// trailing block satisfies it if ITS last statement does (recurse), so
// `int f(){ { return 1; } }` passes. Not full reachability (the Completion
// lattice in Session 2 supersedes this); just enough that a block at the tail
// doesn't trip the check.
bool endsInReturnNode(parse::Node const& s);

// Whether a `break` targeting THIS switch/loop appears in `s` — a break not
// captured by a nested loop/switch. A switch clause containing such a break can
// escape to after the switch, so the switch is NOT a return-terminator even if
// the clause's last statement returns. Mirrors codegen's containsBreak so the
// two stages agree on return-correctness.
bool containsBreak(parse::Node const& s) {
    if (s.kind == parse::Kind::kBreakStmt) return true;
    if (s.kind == parse::Kind::kWhileStmt || s.kind == parse::Kind::kDoWhileStmt
        || s.kind == parse::Kind::kForLongStmt || s.kind == parse::Kind::kForEnumStmt
        || s.kind == parse::Kind::kSwitchStmt) {
        return false;   // a nested loop/switch captures its own breaks
    }
    for (auto const& ch : s.children) if (ch && containsBreak(*ch)) return true;
    return false;
}

bool endsInReturn(std::vector<std::unique_ptr<parse::Node>> const& stmts) {
    if (stmts.empty() || !stmts.back()) return false;
    return endsInReturnNode(*stmts.back());
}

// Whether a single statement guarantees control leaves via a return: a return,
// a block whose last statement does, or an if/else WITH an else where both arms
// do (an else-less if always has a fall-through path). An `else if` chain works
// by recursion — the else-branch is itself a kIfStmt.
bool endsInReturnNode(parse::Node const& s) {
    if (s.kind == parse::Kind::kReturnStmt) return true;
    if (s.kind == parse::Kind::kBlockStmt) return endsInReturn(s.children);
    if (s.kind == parse::Kind::kIfStmt && s.children.size() > 2
        && s.children[2]) {
        return endsInReturnNode(*s.children[1])   // then-branch (a block)
            && endsInReturnNode(*s.children[2]);  // else-branch (block or if)
    }
    // C-style fall-through: control can fall out the bottom of a switch only off
    // the LAST clause's body (every earlier clause either returns or falls into
    // the next). So the switch is a return-terminator iff it has a default, no
    // clause has a break escaping past it, and the LAST clause's body ends in a
    // return — a stacked empty (or non-returning) clause then reaches that final
    // return via fall-through.
    if (s.kind == parse::Kind::kSwitchStmt) {
        bool has_default = false;
        for (std::size_t i = 1; i < s.children.size(); i++) {
            parse::Node const& clause = *s.children[i];
            if (!clause.children[0]) has_default = true;
            if (containsBreak(*clause.children[1])) {
                return false;   // an escaping break reaches past the switch
            }
        }
        if (!has_default) return false;
        return endsInReturnNode(*s.children.back()->children[1]);
    }
    // A non-completing loop (resolve flagged a constant-true condition with no
    // escaping break) never falls through — control leaves only via a return
    // inside it (or not at all), so it is a return-terminator.
    if (s.kind == parse::Kind::kWhileStmt || s.kind == parse::Kind::kDoWhileStmt
        || s.kind == parse::Kind::kForLongStmt) {
        return s.non_completing;
    }
    return false;
}

// Pre-pass over a function's signature: fold-infer each parameter default,
// infer a typeless param's type from it (write-back to the entry + param node),
// and capture the folded default constant on the entry for call-site filling.
void classifyFunctionSignature(parse::Tree& tree, parse::Node& fn,
                               diagnostic::Sink& diag) {
    if (fn.resolved_entry_id < 0) return;
    parse::Entry& e = tree.entries[fn.resolved_entry_id];
    e.param_default_text.assign(e.param_types.size(), "");
    e.param_default_kind.assign(e.param_types.size(), parse::Kind::kProgram);
    for (std::size_t i = 0;
         i < fn.params.size() && i < e.param_types.size(); i++) {
        parse::Node& p = *fn.params[i];
        if (p.children.empty() || !p.children[0]) continue;   // required
        parse::Node& def = *p.children[0];
        inferExpr(tree, def, p.return_type, diag);
        if (!isLiteralKind(def.kind)) {
            diagnostic::report(diag, {def.file_id, def.tok,
                "A parameter default must be a constant expression.", {}});
            continue;
        }
        if (p.return_type.empty()) {
            std::string t = preferredSpelling(def.inferred_type);
            p.return_type = t;
            e.param_types[i] = t;
            if (p.resolved_entry_id >= 0) {
                tree.entries[p.resolved_entry_id].slids_type = t;
            }
        } else if (!literalFitsContext(def, p.return_type)) {
            diagnostic::report(diag, {def.file_id, def.tok,
                "Default value does not fit parameter type '" + p.return_type
                    + "'.", {}});
        }
        e.param_default_text[i] = def.text;
        e.param_default_kind[i] = def.kind;
    }
}

void classifyFunctionBody(parse::Tree& tree, parse::Node& fn,
                          diagnostic::Sink& diag) {
    // No frame push — resolve already handled scope discipline. We just
    // walk stmts and infer types using resolved_entry_id stamped upstream.
    // Complete each nested function's signature first (typeless params +
    // default capture), so a call to it — even before its definition —
    // type-checks against the full signature.
    for (auto& ch : fn.children) {
        if (ch && ch->kind == parse::Kind::kFunctionDef) {
            classifyFunctionSignature(tree, *ch, diag);
        }
    }
    for (auto& ch : fn.children) {
        if (ch) classifyStmt(tree, *ch, fn.return_type, diag);
    }
    // A non-void function must end with a return statement, else codegen
    // would emit an unterminated block. This is the "last statement is a
    // return" heuristic, not full reachability (see todo: revisit non-void
    // function returns). void bodies fall through to an implicit `ret void`.
    if (fn.return_type != "void") {
        if (!endsInReturn(fn.children)) {
            diagnostic::report(diag, {fn.file_id, fn.name_tok,
                "Function '" + fn.name + "' must end with a return statement.", {}});
        }
    }
}

// Type-infer a namespace's members: const inits in their declared-type context,
// member function bodies, and nested namespaces. Mirrors classify::run's
// file-scope handling, recursing through the namespace structure.
void classifyNamespace(parse::Tree& tree, parse::Node& node,
                       diagnostic::Sink& diag) {
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kNamespaceDecl) {
            classifyNamespace(tree, *m, diag);
        } else if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            for (auto& init : m->children) {
                if (init) inferExpr(tree, *init, m->return_type, diag);
            }
        } else if (m->kind == parse::Kind::kFunctionDef) {
            classifyFunctionBody(tree, *m, diag);
        }
    }
}

}  // namespace

void run(parse::Tree& tree, diagnostic::Sink& diag) {
    parse::Node* program = nullptr;
    for (auto& n : tree.nodes) {
        if (n && n->kind == parse::Kind::kProgram) {
            program = n.get();
            break;
        }
    }
    if (!program) return;

    // Signature pre-pass first: a call to a later-defined function must see its
    // completed param types + captured defaults.
    for (auto& ch : program->children) {
        if (ch && (ch->kind == parse::Kind::kFunctionDef
                || ch->kind == parse::Kind::kFunctionDecl)) {
            classifyFunctionSignature(tree, *ch, diag);
        }
    }

    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind == parse::Kind::kFunctionDef) {
            classifyFunctionBody(tree, *ch, diag);
        } else if (ch->kind == parse::Kind::kNamespaceDecl) {
            classifyNamespace(tree, *ch, diag);
        } else if (ch->kind == parse::Kind::kVarDeclStmt && ch->is_const) {
            // Type-infer top-level const init in its declared type's context.
            for (auto& init : ch->children) {
                if (init) inferExpr(tree, *init, ch->return_type, diag);
            }
        }
    }
}

}  // namespace classify
