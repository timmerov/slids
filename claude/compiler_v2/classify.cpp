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

// These predicates read the STRUCTURE off the handle (form / cat / bits) — no
// spelling is produced. Callers pass a type field (a TypeRef) directly; the
// structure is the single source of truth (spellings live only at human edges).

bool isFloatType(widen::TypeRef t) {
    widen::TypeKind k;
    return widen::classify(t, k) && k.cat == widen::Category::kFloat;
}

// A reference type: `T^`. strip() sees through a transparent alias.
bool isReference(widen::TypeRef t) {
    return widen::form(widen::strip(t)) == widen::Type::Form::kPointer;
}

// An iterator type: `T[]`.
bool isIteratorType(widen::TypeRef t) {
    return widen::form(widen::strip(t)) == widen::Type::Form::kIterator;
}

// Any pointer: an iterator (`T[]`), a reference (`T^`), or the typeless null
// (`anyptr`, nullptr's type). Used for truthy-coercion and pointer ops.
bool isPtrLikeType(widen::TypeRef t) {
    widen::Type::Form f = widen::form(widen::strip(t));
    return f == widen::Type::Form::kPointer
        || f == widen::Type::Form::kIterator
        || f == widen::Type::Form::kAnyptr;
}

// The pointee type of a reference/iterator; kNoType for anyptr or a non-pointer.
widen::TypeRef pointeeType(widen::TypeRef t) {
    widen::Type const& ty = widen::get(widen::strip(t));
    if (ty.form == widen::Type::Form::kPointer
     || ty.form == widen::Type::Form::kIterator) {
        return ty.pointee;
    }
    return widen::kNoType;
}

bool isNumericType(widen::TypeRef t);   // defined below

// A buffer-class pointer: a pointer (reference or iterator) whose pointee is a
// generic byte type — `void`, `int8`, `uint8`. These reinterpret to/from any
// pointer type; every other pointer pair must reinterpret indirectly (chain
// through a buffer-class type). `void` is included though it only spells as a
// reference (`void^`): void has no stride, so `void[]` never reaches here.
bool isBufferClassPtr(widen::TypeRef t) {
    widen::TypeRef p = pointeeType(t);
    if (p == widen::kNoType) return false;
    return p == widen::intern("void") || p == widen::intern("int8")
        || p == widen::intern("uint8");
}

// The pointee of a reference / iterator (used below for same-pointee checks).
widen::TypeRef castPointee(widen::TypeRef t) {
    return pointeeType(t);
}

// May a value of type `from` be IMPLICITLY assigned to a pointer-or-intptr
// lvalue of type `to`? Implicit pointer casts only ever STRIP type information
// (the widening direction): nullptr → any pointer, any pointer → `void^` or
// `intptr`, and an iterator demoted to a reference of the same pointee. Adding
// information (`void^`/`intptr` → a typed pointer, reference → iterator) needs
// an explicit `<Type^>`. Caller gates on either side being pointer-ish.
bool ptrImplicitOk(widen::TypeRef from, widen::TypeRef to) {
    from = widen::deepStrip(from);   // compare modulo aliases (Integer^ == IntPtr)
    to = widen::deepStrip(to);
    if (from == to) return true;
    if (from == widen::intern("anyptr")) return isPtrLikeType(to);   // nullptr → any
    if (to == widen::intern("void^"))    return isPtrLikeType(from); // strip to void ref
    if (to == widen::intern("intptr"))   return isPtrLikeType(from); // strip to integer
    // An iterator demotes to a reference of the same pointee (loses arithmetic).
    if (isReference(to) && isIteratorType(from)
        && castPointee(to) == castPointee(from)) return true;
    return false;
}

// Is `t` a legal cast endpoint — a pointer, or the integer type `intptr`? Only
// `intptr` bridges pointers and integers; no other integer type may be cast
// to or from a pointer.
bool isCastEndpoint(widen::TypeRef t) {
    return isPtrLikeType(t) || t == widen::intern("intptr");
}

// May an EXPLICIT `<to> from` cast reinterpret a value of type `from` as `to`?
// Both endpoints must be a pointer or `intptr`. A buffer-class pointer (or
// `intptr`) on either side bridges to any pointer; an iterator and a reference
// of the same pointee reinterpret either way. Two unrelated non-buffer pointers
// may not cast directly (the canonical "chain through void^" rule). On failure,
// `why` is set to a user-facing reason.
bool ptrExplicitOk(widen::TypeRef from, widen::TypeRef to,
                   std::string& why) {
    from = widen::deepStrip(from);   // compare modulo aliases
    to = widen::deepStrip(to);
    widen::TypeRef intptr = widen::intern("intptr");
    if (isNumericType(from) && from != intptr) {
        why = "only 'intptr' may be cast to or from a pointer";
        return false;
    }
    if (isNumericType(to) && to != intptr) {
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
    if (from == widen::intern("anyptr")) return true;     // nullptr → any pointer
    if (from == intptr || to == intptr) return true;      // pointer ↔ integer
    if (isBufferClassPtr(from) || isBufferClassPtr(to)) return true;  // buffer ↔ any
    if (((isReference(from) && isIteratorType(to))
      || (isIteratorType(from) && isReference(to)))
        && castPointee(from) == castPointee(to)) return true;  // iterator ↔ reference
    why = "reinterpret indirectly through 'void^'";
    return false;
}

// A fixed-size array type (`int[5]`, `int[3][5]`), distinct from `int[]`
// (iterator) and `int^` (ref). strip() sees through a transparent alias.
bool isArrayType(widen::TypeRef t) {
    return widen::form(widen::strip(t)) == widen::Type::Form::kArray;
}

// The leftmost (outermost, standard row-major) dimension's size — the dimension
// one subscript consumes. `int[3][5]` -> 3 (3 rows of int[5]).
int arrayFirstDim(widen::TypeRef t) {
    return widen::get(widen::strip(t)).dims.front();
}

// The type after one subscript: strip the leftmost `[N]`. `int[3][5]` ->
// `int[5]`; `int[5]` -> `int`. Built STRUCTURALLY (preserves an alias element).
widen::TypeRef arrayElementType(widen::TypeRef t) {
    widen::Type const& a = widen::get(widen::strip(t));
    if (a.dims.size() <= 1) return a.elem;
    return widen::internArray(
        a.elem, std::vector<int>(a.dims.begin() + 1, a.dims.end()));
}

bool isIntegerClass(widen::TypeRef t) {
    widen::TypeKind k;
    if (!widen::classify(t, k)) return false;
    return k.cat != widen::Category::kFloat;
}


bool isNumericType(widen::TypeRef t) {
    widen::TypeKind k;
    return widen::classify(t, k);
}

// Validate a `(Type=src)` conversion against the value-conversion grid. LEAF
// rule is the existing scalar grid (numeric<->numeric always; pointer<->{bool,
// intptr} only). Recurses per-slot for tuple targets and per-element for array
// targets — a tuple converts iff the source is a tuple of the same arity and
// each slot pair converts; an array iff the source is an array of the same
// dims and the element types convert. A class target is deferred (no op= yet).
// Diagnostics carry the outer conv tok; per-slot caret refinement is a later
// pass. Returns true iff a diagnostic fired (cascade-suppression hint for the
// caller; not currently consumed).
bool checkConvertCompat(widen::TypeRef dst, widen::TypeRef src,
                        int file_id, int tok, diagnostic::Sink& diag) {
    using F = widen::Type::Form;
    widen::TypeRef ds = widen::strip(dst);
    widen::TypeRef ss = widen::strip(src);
    F df = widen::form(ds);
    F sf = widen::form(ss);
    std::string to = widen::spellOrEmpty(dst);
    std::string from = widen::spellOrEmpty(src);

    if (isPtrLikeType(dst)) {
        diagnostic::report(diag, {file_id, tok,
            "A type conversion target may not be a pointer type.", {}});
        return true;
    }
    if (df == F::kVoid) {
        diagnostic::report(diag, {file_id, tok,
            "Cannot convert to '" + to + "'; the target must be a value type.", {}});
        return true;
    }
    if (df == F::kTuple) {
        if (sf != F::kTuple) {
            diagnostic::report(diag, {file_id, tok,
                "Cannot convert '" + from + "' to '" + to
                + "'; the source must be a tuple.", {}});
            return true;
        }
        auto const& dslots = widen::get(ds).slots;
        auto const& sslots = widen::get(ss).slots;
        if (dslots.size() != sslots.size()) {
            diagnostic::report(diag, {file_id, tok,
                "Cannot convert '" + from + "' to '" + to + "'; slot count differs ("
                + std::to_string(sslots.size()) + " vs "
                + std::to_string(dslots.size()) + ").", {}});
            return true;
        }
        bool any = false;
        for (std::size_t i = 0; i < dslots.size(); i++) {
            if (checkConvertCompat(dslots[i], sslots[i], file_id, tok, diag)) any = true;
        }
        return any;
    }
    if (df == F::kArray) {
        if (sf != F::kArray) {
            diagnostic::report(diag, {file_id, tok,
                "Cannot convert '" + from + "' to '" + to
                + "'; the source must be an array.", {}});
            return true;
        }
        auto const& ddims = widen::get(ds).dims;
        auto const& sdims = widen::get(ss).dims;
        if (ddims != sdims) {
            diagnostic::report(diag, {file_id, tok,
                "Cannot convert '" + from + "' to '" + to
                + "'; array shape differs.", {}});
            return true;
        }
        return checkConvertCompat(widen::get(ds).elem, widen::get(ss).elem,
                                  file_id, tok, diag);
    }
    if (df == F::kSlid) {
        diagnostic::report(diag, {file_id, tok,
            "Cannot convert to class type '" + to
            + "'; conversion to a class is not yet implemented.", {}});
        return true;
    }
    // df is kPrimitive (the leaf). Validate the scalar grid.
    if (isPtrLikeType(src)) {
        if (ds != widen::intern("bool") && ds != widen::intern("intptr")) {
            diagnostic::report(diag, {file_id, tok,
                "Cannot convert '" + from + "' to '" + to
                + "'; a pointer converts only to 'bool' or 'intptr'.", {}});
            return true;
        }
        return false;
    }
    if (!isNumericType(src)) {
        diagnostic::report(diag, {file_id, tok,
            "Cannot convert '" + from + "' to '" + to + "'.", {}});
        return true;
    }
    // Both numeric — the value grid permits all such conversions.
    return false;
}

// `!` and the logical operators truthy-coerce: numerics via cmp-against-zero,
// pointer-like via cmp-against-null. Void (and any future non-value type) is
// rejected here.
bool isCoercibleToBool(widen::TypeRef t) {
    return isNumericType(t) || isPtrLikeType(t);
}

// Text suitable for widen::intLiteralFits / floatLiteralFits — bool "true"/"false"
// → "1"/"0"; everything else passes through.
std::string literalTextForFit(parse::Node const& n) {
    if (n.kind == parse::Kind::kBoolLiteral) return (n.text == "true") ? "1" : "0";
    return n.text;
}

// Per-kind default TYPE-REF when a literal has no usable context: the preferred
// handle for its kind at the minimal width that holds the value — the same handle
// ##type reports. The 32-bit tier mints the NO-WIDTH primitive (int / uint / float),
// not the explicit-width one (int32 / uint32 / float32), so a weak literal that
// can't flex into its partner carries `int`, not `int32`, into the widening — they
// are distinct TypeRefs (primitives key by name). 64-bit handles carry real width.
widen::TypeRef defaultLiteralType(parse::Node const& n) {
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
                if (mag <= static_cast<uint64_t>(INT32_MAX) + 1) return widen::intern("int");
                return widen::intern("int64");
            }
            if (mag <= static_cast<uint64_t>(INT32_MAX)) return widen::intern("int");
            if (mag <= static_cast<uint64_t>(INT64_MAX)) return widen::intern("int64");
            return widen::intern("uint64");
        }
        case parse::Kind::kUintLiteral: {
            std::string const& s = n.text;
            errno = 0;
            char* end = nullptr;
            uint64_t mag = std::strtoull(s.c_str(), &end, 10);
            assert(!s.empty() && end != s.c_str() && *end == '\0'
                && errno != ERANGE
                && "defaultLiteralType: malformed uint text from numeric");
            if (mag <= static_cast<uint64_t>(UINT32_MAX)) return widen::intern("uint");
            return widen::intern("uint64");
        }
        case parse::Kind::kCharLiteral:  return widen::intern("char");
        case parse::Kind::kBoolLiteral:  return widen::intern("bool");
        case parse::Kind::kFloatLiteral: return widen::intern("float");
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
        case parse::Kind::kMoveStmt:
        case parse::Kind::kSwapStmt:
        case parse::Kind::kDestructureStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kMethodCallStmt:
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
        case parse::Kind::kTupleExpr:
        case parse::Kind::kCastExpr:
        case parse::Kind::kConvertExpr:
        case parse::Kind::kNewExpr:
        case parse::Kind::kDeleteStmt:
        case parse::Kind::kDtorCallStmt:
        case parse::Kind::kSizeofExpr:
        case parse::Kind::kStringifyType:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kBlockStmt:
        case parse::Kind::kIfStmt:
        case parse::Kind::kWhileStmt:
        case parse::Kind::kDoWhileStmt:
        case parse::Kind::kForLongStmt:
        case parse::Kind::kForEnumStmt:
        case parse::Kind::kForArrayStmt:
        case parse::Kind::kForTupleStmt:
        case parse::Kind::kForRangedStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kSwitchStmt:
        case parse::Kind::kCaseClause:
        case parse::Kind::kParam:
        case parse::Kind::kClassDef:
        case parse::Kind::kFieldExpr:
            assert(false && "defaultLiteralType: not a literal kind");
            __builtin_unreachable();
    }
    assert(false && "defaultLiteralType: unhandled parse::Kind");
    __builtin_unreachable();
}

bool literalFitsContext(parse::Node const& lit, widen::TypeRef context) {
    if (context == widen::kNoType) return false;
    if (lit.kind == parse::Kind::kFloatLiteral) {
        return widen::floatLiteralFits(lit.text, context);
    }
    return widen::intLiteralFits(literalTextForFit(lit), context);
}

// The single weak/strong test. A WEAK literal (a bare / typeless-const literal, no
// strong_type) flexes to fit wherever it lands; a STRONG-const literal (constfold
// stamped strong_type from a fixed-width declared type) is a TYPED value and never
// flexes — it obeys the same widen/narrow rules a variable of that type would.
bool literalFlexes(parse::Node const& lit) {
    return isLiteralKind(lit.kind) && lit.strong_type == widen::kNoType;
}

// The type a literal NODE takes in `context` — the one source of truth for the
// strong/weak typing split. A strong-const literal keeps its declared (strong) type
// (so a narrowing then errors at the assignment, exactly as a variable's would); a
// weak literal flexes into `context` when it fits, else its preferred no-width default.
widen::TypeRef literalContextType(parse::Node const& lit, widen::TypeRef context) {
    if (!literalFlexes(lit)) return lit.strong_type;        // typed value: its strong type
    if (literalFitsContext(lit, context)) return context;   // weak literal flexes in
    return defaultLiteralType(lit);                         // weak default
}

void inferExpr(parse::Tree& tree, parse::Node& e,
               widen::TypeRef context, diagnostic::Sink& diag);
void classifyFunctionBody(parse::Tree& tree, parse::Node& fn,
                          diagnostic::Sink& diag);
void classifyNamespace(parse::Tree& tree, parse::Node& node,
                       diagnostic::Sink& diag);

// Type-check a class's member-function bodies — ctor, dtor, and methods (all
// kFunctionDef, all self-bound) — recursing into HOISTED classes (whose bodies
// must be typed too, else desugar lowers an un-typed field access).
void classifyClassMemberBodies(parse::Tree& tree, parse::Node& node,
                               diagnostic::Sink& diag) {
    parse::forEachHoistedClass(node,
        [&](parse::Node& cls) {
            for (auto& m : cls.children)
                if (m && m->kind == parse::Kind::kFunctionDef)
                    classifyFunctionBody(tree, *m, diag);
        },
        [](parse::Node&) {});
}

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
    inferExpr(tree, e, widen::kNoType, diag);
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

// Literal-flex preamble for non-shift binaries: when one operand is a WEAK literal
// and the other is not, try-flex the literal into the partner's type. A STRONG-const
// literal (carries strong_type) does NOT flex — it is a typed value, so it keeps its
// declared type and widens via commonType like a variable of that type would (a
// narrowing of the result back into a narrow partner then errors at the assignment).
void flexBinaryOperands(parse::Node& lhs, parse::Node& rhs) {
    bool lhs_lit = literalFlexes(lhs);
    bool rhs_lit = literalFlexes(rhs);
    if (lhs_lit && !rhs_lit && rhs.inferred_type != widen::kNoType) {
        if (literalFitsContext(lhs, rhs.inferred_type)) {
            lhs.inferred_type = rhs.inferred_type;
        }
    } else if (rhs_lit && !lhs_lit && lhs.inferred_type != widen::kNoType) {
        if (literalFitsContext(rhs, lhs.inferred_type)) {
            rhs.inferred_type = lhs.inferred_type;
        }
    }
}

// Two types occupy the same width-class (so `int` and `int32` are "the same" for
// overload-exactness, sidestepping the int/int32 spelling). Non-numeric types
// (char[], slids) fall back to TypeRef equality (the arena dedups structurally).
bool sameClass(widen::TypeRef a, widen::TypeRef b) {
    widen::TypeKind ka, kb;
    if (!widen::classify(a, ka) || !widen::classify(b, kb)) return a == b;
    return ka.cat == kb.cat && ka.bits == kb.bits;
}

// Silent recursive shape-match predicate for the assignment relation. Walks
// (dst, src) in lockstep; returns true iff dims/arity match at every composite
// level AND each leaf is type-compatible at a KIND level (both numeric, both
// pointer-like, or the same class). The per-leaf widen/narrow/cross-family
// reject is deferred to codegen's widen::convert; this predicate is the
// classify-side "viable for cost rank" used by overload resolution. Aliases
// transparent via strip().
bool shapesAndLeavesMatch(widen::TypeRef dst, widen::TypeRef src) {
    using F = widen::Type::Form;
    widen::TypeRef ds = widen::strip(dst);
    widen::TypeRef ss = widen::strip(src);
    F df = widen::form(ds);
    F sf = widen::form(ss);
    if (df == F::kArray || sf == F::kArray) {
        if (df != F::kArray || sf != F::kArray) return false;
        if (widen::get(ds).dims != widen::get(ss).dims) return false;
        return shapesAndLeavesMatch(widen::get(ds).elem, widen::get(ss).elem);
    }
    if (df == F::kTuple || sf == F::kTuple) {
        if (df != F::kTuple || sf != F::kTuple) return false;
        std::vector<widen::TypeRef> const& dslots = widen::get(ds).slots;
        std::vector<widen::TypeRef> const& sslots = widen::get(ss).slots;
        if (dslots.size() != sslots.size()) return false;
        for (std::size_t i = 0; i < dslots.size(); i++)
            if (!shapesAndLeavesMatch(dslots[i], sslots[i])) return false;
        return true;
    }
    // Leaf: same width-class (covers numeric + same-handle class/primitive), OR
    // both pointer-like (ptr/iter/anyptr). Cross-family / pointer-vs-numeric
    // mismatches fall through to "not viable".
    if (sameClass(ds, ss)) return true;
    widen::TypeKind dk, sk;
    if (widen::classify(ds, dk) && widen::classify(ss, sk)) return true;
    if (widen::form(ds) == F::kPointer || widen::form(ds) == F::kIterator
        || widen::form(ds) == F::kAnyptr) {
        if (widen::form(ss) == F::kPointer || widen::form(ss) == F::kIterator
            || widen::form(ss) == F::kAnyptr) return true;
    }
    return false;
}

// A NON-PRIMITIVE value (class / tuple / array) passed as a function or method
// ARGUMENT is auto-promoted to a reference: it is passed by reference even when
// written by value (codegen materializes the value + passes its address — see
// emitCall/isValueByRef). When the parameter is a reference (`T^`) and the arg is
// a non-primitive value, the arg is therefore checked against the POINTEE `T`, not
// the reference. Returns that pointee, or kNoType when no promotion applies (a
// primitive, an already-pointer arg, or a non-reference parameter). Promotion is
// argument-passing only; it does not apply at decls, assigns, stores, or returns.
widen::TypeRef autoRefPointee(widen::TypeRef param, parse::Node const& arg) {
    using F = widen::Type::Form;
    if (widen::form(widen::strip(param)) != F::kPointer) return widen::kNoType;
    F af = widen::form(widen::strip(arg.inferred_type));
    if (af != F::kSlid && af != F::kTuple && af != F::kArray) return widen::kNoType;
    return widen::get(widen::strip(param)).pointee;
}

// Conversion cost of argument `a` to parameter type `param` for overload ranking:
// 0 = exact (same class), 1 = a widening (literal flex, or within-family widen),
// -1 = not convertible (narrowing / cross-family / un-typeable).
int argConvertCost(parse::Node const& a, widen::TypeRef param) {
    widen::TypeRef at = widen::strip(a.inferred_type);   // see through one alias layer
    // A non-primitive value auto-promotes to a reference param: viable when the
    // arg type matches the param's pointee (exact, mirroring the value checks).
    // SHAPE-MATCH with elem widen ranks as cost 1 (per-leaf widen, like a scalar
    // widening); exact match still wins overloads.
    if (param != widen::kNoType) {
        widen::TypeRef pointee = autoRefPointee(param, a);
        if (pointee != widen::kNoType) {
            if (sameClass(at, widen::strip(pointee))) return 0;
            if (shapesAndLeavesMatch(widen::strip(pointee), at)) return 1;
            return -1;
        }
    }
    if (at == widen::kNoType) return -1;
    // An already-a-pointer arg vs a pointer param: rank by the implicit-pointer
    // relation, which deep-strips const/alias — so a `T^` arg matches a munged
    // `(const T)^` param (const is transparent; no enforcement yet).
    if (isPtrLikeType(at) && isPtrLikeType(param)) {
        return ptrImplicitOk(at, param) ? 0 : -1;
    }
    if (sameClass(at, param)) return 0;
    if (literalFlexes(a)) {
        return literalFitsContext(a, param) ? 1 : -1;   // weak literal flexes into param
    }
    // A strong-const literal (and any non-literal) ranks as a typed value: exact, a
    // within-family widen, else not viable — so a narrowing arg is rejected.
    widen::TypeRef out;
    if (widen::commonType(at, param, out) && sameClass(out, param)) return 1;
    return -1;
}

// Resolve a (possibly overloaded) user-function call: infer args, pick the best
// candidate, stamp the chosen signature. Defined after inferExpr (mutually
// recursive — it infers the argument expressions).
void classifyCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);
void checkStrongConstAssign(widen::TypeRef dest, parse::Node const& rhs,
                            diagnostic::Sink& diag);
void checkSlidAssign(widen::TypeRef lvalue_type, parse::Node const& rhs,
                     diagnostic::Sink& diag);
// Validate + type a tuple literal initializing an ARRAY (defined after inferExpr);
// reused for an array-typed TUPLE SLOT.
void classifyArrayFromTuple(parse::Tree& tree, widen::TypeRef declType,
                            parse::Node& rhs, diagnostic::Sink& diag);
// The one ASSIGNMENT RELATION (defined just before classifyStmt): the canonical
// post-inference check run at every assignment-family site.
void checkValueAssign(parse::Tree& tree, widen::TypeRef dest, parse::Node& rhs,
                      diagnostic::Sink& diag);
// Build a constructed value for a class from an optional init (defined later).
std::unique_ptr<parse::Node> constructClass(parse::Tree& tree,
                                            parse::ClassInfo const& info,
                                            std::unique_ptr<parse::Node> init,
                                            int file_id, int tok,
                                            diagnostic::Sink& diag);

void inferExpr(parse::Tree& tree, parse::Node& e,
               widen::TypeRef context, diagnostic::Sink& diag) {
    switch (e.kind) {
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral: {
            e.inferred_type = literalContextType(e, context);
            return;
        }
        case parse::Kind::kStringLiteral: {
            e.inferred_type = widen::intern("char[]");
            return;
        }
        case parse::Kind::kNullptrLiteral: {
            // nullptr takes the pointer type in context (`int^ p = nullptr`);
            // with no pointer context it is the typeless null `anyptr`, which
            // coerces in comparisons against any pointer.
            e.inferred_type = isPtrLikeType(context) ? context : widen::intern("anyptr");
            return;
        }
        case parse::Kind::kAddrOfExpr: {
            // `^lvalue` -> a pointer to the operand. The category is operand-
            // driven: a bare variable yields a reference (`T^`); an indexed
            // array element yields an iterator (`T[]`).
            assert(e.children.size() == 1 && "kAddrOfExpr needs 1 operand");
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, widen::kNoType, diag);
            if (operand.inferred_type != widen::kNoType) {
                bool indexed = (operand.kind == parse::Kind::kIndexExpr);
                e.inferred_type = indexed                         // structural — keeps an
                    ? widen::internIterator(operand.inferred_type)  // alias pointee intact
                    : widen::internPointer(operand.inferred_type);
            }
            return;
        }
        case parse::Kind::kIndexExpr: {
            // `base[index]` -> an element. The base must be an array; the result
            // strips one (leftmost) dimension. A constant index is bounds-checked.
            assert(e.children.size() == 2 && "kIndexExpr needs base + index");
            parse::Node& base = *e.children[0];
            parse::Node& index = *e.children[1];
            inferExpr(tree, base, widen::kNoType, diag);
            inferExpr(tree, index, widen::kNoType, diag);
            // Implicit deref for the ARRAY-BY-POINTER param shorthand: a param
            // typed `int[3]^` (rewritten by mungeParamType) indexes WITHOUT an
            // explicit `^`. Treat the base as the pointee array for the rest of
            // the index logic.
            {
                widen::TypeRef cs = widen::strip(base.inferred_type);
                if (widen::form(cs) == widen::Type::Form::kPointer) {
                    widen::TypeRef pointee = widen::get(cs).pointee;
                    if (isArrayType(pointee)) base.inferred_type = pointee;
                }
            }
            std::string bt = widen::spellOrEmpty(base.inferred_type);
            // A tuple slot read. Slots are heterogeneous, so the result type
            // depends on a STATIC index — the index must be a compile-time
            // constant (a runtime index is array subscript, not a tuple slot).
            if (widen::form(widen::strip(base.inferred_type))
                == widen::Type::Form::kTuple) {
                std::vector<widen::TypeRef> slots =
                    widen::get(widen::strip(base.inferred_type)).slots;
                bool const_idx = index.kind == parse::Kind::kIntLiteral
                              || index.kind == parse::Kind::kUintLiteral
                              || index.kind == parse::Kind::kCharLiteral;
                if (!const_idx) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "A tuple slot index must be a compile-time constant.", {}});
                    return;
                }
                long idx = std::strtol(index.text.c_str(), nullptr, 10);
                if (idx < 0 || idx >= static_cast<long>(slots.size())) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Tuple slot index " + std::to_string(idx)
                        + " is out of bounds for '" + bt + "'.", {}});
                    return;
                }
                e.inferred_type = slots[idx];
                return;
            }
            bool array = isArrayType(base.inferred_type);
            bool iter = isIteratorType(base.inferred_type);
            if (!bt.empty() && !array && !iter) {
                // Over-indexing: the base is itself a subscript that already
                // bottomed out at a scalar (`a[i][j][k]` on a 2-D array, or the
                // comma form `a[i,j,k]`). Name the over-indexed type rather than
                // the incidental scalar. A genuine scalar subscript (`s[0]`) keeps
                // the plain message.
                if (base.kind == parse::Kind::kIndexExpr) {
                    parse::Node const* root = &base;
                    while (root->kind == parse::Kind::kIndexExpr) {
                        root = root->children[0].get();
                    }
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Subscript indexes past the last dimension of '"
                        + widen::spellOrEmpty(root->inferred_type) + "'.", {}});
                    return;
                }
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Cannot subscript a non-array value of type '" + bt + "'.",
                    {}});
                return;
            }
            if (index.inferred_type != widen::kNoType
                && !isIntegerClass(index.inferred_type)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "An array index must be an integer; got '"
                    + widen::spellOrEmpty(index.inferred_type) + "'.", {}});
            }
            // A constant integer index is bounds-checked against a fixed-size
            // array; an iterator has no known length, so no check. char literals
            // carry their numeric codepoint as text (so a `char` index counts).
            if (array
                && (index.kind == parse::Kind::kIntLiteral
                    || index.kind == parse::Kind::kUintLiteral
                    || index.kind == parse::Kind::kCharLiteral)) {
                long idx = std::strtol(index.text.c_str(), nullptr, 10);
                int dim = arrayFirstDim(base.inferred_type);
                if (idx < 0 || idx >= dim) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Array index " + std::to_string(idx)
                        + " is out of bounds for '" + bt + "'.", {}});
                }
            }
            e.inferred_type = bt.empty()  ? widen::kNoType
                            : array       ? arrayElementType(base.inferred_type)
                                          : pointeeType(base.inferred_type);
            return;
        }
        case parse::Kind::kTupleExpr: {
            // A tuple literal `(e0, e1, ...)`. Each slot is inferred; if the
            // context is a tuple of the same arity, each slot gets its declared
            // slot type as context (so a literal flexes into it). The result type
            // is the kTuple of the slots' inferred types.
            widen::TypeRef ctx_s = widen::strip(context);
            // A tuple LITERAL whose context is an ARRAY is array-from-tuple, not a
            // nested tuple: type it as the array and leave the single per-element
            // validation to checkValueAssign -> classifyArrayFromTuple. Inferring it
            // as a tuple here would mis-type the node `(int,int,int)` and force a
            // second, contradictory classification (the double-classification).
            if (widen::form(ctx_s) == widen::Type::Form::kArray) {
                e.inferred_type = context;
                return;
            }
            std::vector<widen::TypeRef> ctx_slots;
            if (widen::form(ctx_s) == widen::Type::Form::kTuple
                && widen::get(ctx_s).slots.size() == e.children.size()) {
                ctx_slots = widen::get(ctx_s).slots;
            }
            std::vector<widen::TypeRef> slots;
            for (std::size_t i = 0; i < e.children.size(); i++) {
                widen::TypeRef slot_ctx =
                    ctx_slots.empty() ? widen::kNoType : ctx_slots[i];
                // An ARRAY-typed slot taking a tuple LITERAL is array-from-tuple,
                // not a nested tuple: validate it as such and type the slot as the
                // array (so construction builds an array value, not a sub-tuple).
                if (slot_ctx != widen::kNoType
                    && widen::form(widen::strip(slot_ctx)) == widen::Type::Form::kArray
                    && e.children[i]->kind == parse::Kind::kTupleExpr) {
                    classifyArrayFromTuple(tree, slot_ctx, *e.children[i], diag);
                    e.children[i]->inferred_type = slot_ctx;
                } else {
                    inferExpr(tree, *e.children[i], slot_ctx, diag);
                }
                slots.push_back(e.children[i]->inferred_type);
            }
            e.inferred_type = widen::internTuple(slots);
            return;
        }
        case parse::Kind::kDerefExpr: {
            // `value^` -> the pointee. The operand must be a pointer.
            assert(e.children.size() == 1 && "kDerefExpr needs 1 operand");
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, widen::kNoType, diag);
            std::string ot = widen::spellOrEmpty(operand.inferred_type);
            if (!ot.empty() && !isPtrLikeType(operand.inferred_type)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Cannot dereference a non-pointer value of type '"
                    + ot + "'.", {}});
                return;
            }
            e.inferred_type = pointeeType(operand.inferred_type);
            return;
        }
        case parse::Kind::kFieldExpr: {
            // `base.field` -> a named field of a class lvalue. The base resolves
            // to a class type (a kSlid carrying its slots); look the field up in
            // the class's ClassInfo to get its slot type. desugar lowers this to
            // a slot index.
            assert(e.children.size() == 1 && "kFieldExpr needs a base");
            parse::Node& base = *e.children[0];
            inferExpr(tree, base, widen::kNoType, diag);
            widen::TypeRef bt = widen::strip(base.inferred_type);
            if (bt == widen::kNoType) return;   // base already reported
            if (widen::form(bt) != widen::Type::Form::kSlid) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Cannot access field '" + e.name + "' of non-class value of "
                    "type '" + widen::spellOrEmpty(base.inferred_type) + "'.", {}});
                return;
            }
            auto it = tree.classes.find(bt);
            if (it == tree.classes.end()) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Unknown class type '" + widen::get(bt).name + "'.", {}});
                return;
            }
            int idx = it->second.fieldIndex(e.name);
            if (idx < 0) {
                diagnostic::report(diag, {e.file_id, e.name_tok,
                    "Class '" + it->second.name + "' has no field '" + e.name
                    + "'.", {}});
                return;
            }
            e.inferred_type = it->second.field_types[idx];
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
                std::string ty = widen::spellOrEmpty(e.return_type);
                if (ty.empty()) {
                    parse::Node& operand = *e.children[0];
                    inferExpr(tree, operand, widen::kNoType, diag);
                    ty = widen::spellOrEmpty(operand.inferred_type);
                }
                // An empty type rides an already-reported upstream error (an
                // un-typeable operand); fall to 0 silently rather than cascade.
                size = ty.empty() ? 0 : widen::typeByteSize(ty);
                if (size < 0) {
                    // A class's size is not a compile-time constant — it is the
                    // real struct layout LLVM owns. Rewrite to a call to the class's
                    // `<Name>__$sizeof()` helper (GEP-null/ptrtoint, the v1 design);
                    // codegen emits that function per class. Result is a runtime
                    // intptr, NOT foldable (so it can't init a const).
                    widen::TypeRef st = e.return_type != widen::kNoType
                        ? widen::strip(e.return_type)
                        : widen::strip(e.children[0]->inferred_type);
                    if (widen::form(st) == widen::Type::Form::kSlid
                        && tree.classes.count(st)) {
                        widen::TypeRef iptr = widen::intern("intptr");
                        e.kind = parse::Kind::kCallExpr;
                        e.name = widen::classSymbol(st) + "__$sizeof";
                        e.return_type = iptr;   // emitCall reads this for the ret llty
                        e.children.clear();
                        e.param_types.clear();
                        e.inferred_type = iptr;
                        return;
                    }
                    // void / an unregistered slid has no size at all.
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Cannot take sizeof of '" + ty + "'.", {}});
                    size = 0;
                }
            } else {
                size = static_cast<long long>(e.children[0]->text.size()) + 1;
            }
            e.kind = parse::Kind::kIntLiteral;
            e.text = std::to_string(size);
            e.return_type = widen::kNoType;
            e.children.clear();
            e.inferred_type = widen::intern("intptr");
            return;
        }
        case parse::Kind::kNewExpr: {
            // new T -> T^; new T[n] -> T[]. The array-size must be integer-class;
            // a placement address (children[1]) must be a buffer-class pointer;
            // the element type must be statically sized (Phase 4: a primitive — a
            // slid's size lands with classes).
            std::string elem = widen::spellOrEmpty(e.return_type);
            bool is_array = e.children[0] != nullptr;
            // The `!inferred_type.empty()` guards below skip an operand whose type
            // is empty (an upstream error already reported it) — don't cascade.
            if (is_array) {
                parse::Node& size = *e.children[0];
                inferExpr(tree, size, widen::intern("intptr"), diag);
                if (size.inferred_type != widen::kNoType
                    && !isIntegerClass(size.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "An array size must be an integer; got '"
                        + widen::spellOrEmpty(size.inferred_type) + "'.", {}});
                }
            }
            if (e.children[1]) {
                parse::Node& addr = *e.children[1];
                inferExpr(tree, addr, widen::kNoType, diag);
                if (addr.inferred_type != widen::kNoType
                    && !isBufferClassPtr(addr.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "A placement address must be a buffer-class pointer "
                        "(void^, int8^, uint8^); got '"
                        + widen::spellOrEmpty(addr.inferred_type)
                        + "'.", {}});
                }
            }
            widen::TypeRef es = widen::strip(e.return_type);
            bool is_class = widen::form(es) == widen::Type::Form::kSlid
                && tree.classes.count(es) > 0;
            // A primitive / array / pointer has a static byte size; a CLASS is sized
            // at runtime via its __$sizeof() helper, so it IS allocatable. void / an
            // unregistered slid has no size at all.
            if (widen::typeByteSize(e.return_type) < 0 && !is_class) {
                diagnostic::report(diag, {e.file_id, e.name_tok,
                    "Cannot allocate '" + elem + "'.", {}});
            }
            // Constructor args (children[2]) belong to a SINGLE class object.
            parse::Node* args = (e.children.size() > 2) ? e.children[2].get() : nullptr;
            if (args && (!is_class || is_array)) {
                diagnostic::report(diag, {args->file_id, args->tok,
                    is_array
                      ? "An array allocation cannot take constructor arguments."
                      : "Only a class takes constructor arguments; '" + elem
                            + "' cannot.", {}});
            } else if (is_class) {
                // Construct the heap object: normalize the args (or defaults) into a
                // per-field construction tuple, exactly like a class var-decl. Stored
                // on children[2] for codegen to field-init at the heap pointer — a
                // single object from the args; each ARRAY element default-constructed
                // (the same default value laid into every slot, then its ctor run).
                parse::ClassInfo const& info = tree.classes.at(es);
                std::unique_ptr<parse::Node> init;
                if (!is_array && e.children.size() > 2) init = std::move(e.children[2]);
                auto built = constructClass(tree, info, std::move(init),
                                            e.file_id, e.tok, diag);
                if (e.children.size() <= 2) e.children.resize(3);
                e.children[2] = std::move(built);
            }
            e.inferred_type = is_array ? widen::internIterator(e.return_type)
                                       : widen::internPointer(e.return_type);
            return;
        }
        case parse::Kind::kCastExpr: {
            // `<Type^> operand` — a pointer reinterpret. resolve resolved the
            // target onto return_type. Infer the operand with NO context (a cast
            // reinterprets the operand's own type; it does not flex it), then
            // check the explicit-cast rules. The cast's type IS the target.
            assert(e.children.size() == 1 && "kCastExpr needs 1 operand");
            parse::Node& operand = *e.children[0];
            // `<const>` / `<mutable>` — a qualifier-only cast: keep the operand's
            // pointer type, add / remove const. Applies to a reference or iterator
            // (canon: const is added to / removed from "pointers"). The value is
            // unchanged (const is erased in IR), so codegen needs no special case.
            if (e.text == "const" || e.text == "mutable") {
                inferExpr(tree, operand, widen::kNoType, diag);
                if (operand.inferred_type != widen::kNoType) {
                    if (!isPtrLikeType(operand.inferred_type)) {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "A '<" + e.text + ">' qualifier cast applies only to a "
                            "pointer (reference / iterator); got '"
                            + widen::spellOrEmpty(operand.inferred_type) + "'.", {}});
                        e.inferred_type = operand.inferred_type;   // recover
                        return;
                    }
                    e.inferred_type = (e.text == "const")
                        ? widen::internConst(operand.inferred_type)
                        : widen::removeConst(operand.inferred_type);
                }
                return;
            }
            inferExpr(tree, operand, widen::kNoType, diag);
            std::string to = widen::spellOrEmpty(e.return_type);
            // An empty operand type means inferExpr already reported an error;
            // skip the rule check (it would cascade a second, misleading
            // diagnostic) but still stamp the target type so downstream stages
            // see a typed node — the cast's type IS the target regardless.
            if (operand.inferred_type != widen::kNoType) {
                std::string why;
                std::string ot = widen::spellOrEmpty(operand.inferred_type);
                if (!ptrExplicitOk(operand.inferred_type, e.return_type, why)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Cannot cast '" + ot + "' to '" + to
                        + "'; " + why + ".", {}});
                }
            }
            // The cast's type IS the (resolved) target — keep its handle, don't
            // re-intern the spelling (that would clobber an alias target to a slid).
            e.inferred_type = e.return_type;
            return;
        }
        case parse::Kind::kConvertExpr: {
            // `(Type=operand)` value conversion. Infer the operand with NO
            // context (the conversion explicitly retypes it; it does not flex),
            // then dispatch the (target, source) pair to checkConvertCompat: a
            // leaf target uses the scalar grid; tuple/array targets recurse
            // per-slot / per-element; pointer/void/class targets are rejected.
            // A literal operand was already folded in constfold.
            assert(e.children.size() == 1 && "kConvertExpr needs 1 operand");
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, widen::kNoType, diag);
            if (operand.inferred_type != widen::kNoType) {
                checkConvertCompat(e.return_type, operand.inferred_type,
                                   e.file_id, e.tok, diag);
            }
            // Stamp the target type even on the error paths above, so downstream
            // stages see a typed node — the conversion's type IS the target
            // regardless. Moot when there was an error (no codegen), but uniform
            // with kCastExpr. user notified, accepts state.
            e.inferred_type = e.return_type;
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
            if (e.return_type != widen::kNoType) {
                e.text = widen::spellOrEmpty(e.return_type);
                e.return_type = widen::kNoType;
                e.children.clear();
                e.kind = parse::Kind::kStringLiteral;
                e.inferred_type = widen::intern("char[]");
                return;
            }
            parse::Node& operand = *e.children[0];
            inferExpr(tree, operand, widen::kNoType, diag);
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
                    + (ce.alias_label.empty() ? widen::spellOrEmpty(ce.slids_type) : ce.alias_label);
            } else if (!operand.alias_label.empty()) {
                e.text = operand.alias_label;
            } else if (isLiteralKind(operand.kind)) {
                // A bare literal reports the no-width preferred spelling
                // (int/uint/float), consistent with const-capture and the
                // no-width preference — a value of a DECLARED width keeps its
                // width name (the else below). char has no width-name to fold.
                e.text = preferredSpelling(widen::spellOrEmpty(operand.inferred_type));
            } else {
                e.text = widen::spellOrEmpty(operand.inferred_type);
            }
            e.children.clear();
            e.kind = parse::Kind::kStringLiteral;
            e.inferred_type = widen::intern("char[]");
            return;
        }
        case parse::Kind::kIdentExpr: {
            assert(e.resolved_entry_id >= 0
                && "inferExpr kIdentExpr: resolve did not stamp resolved_entry_id");
            e.inferred_type = parse::entryType(tree, e.resolved_entry_id);
            // The alias label is the type itself when it's a (scalar) alias —
            // drives binaryLabel's sticky-alias rule; the kAlias type is the
            // source of truth, this label is derived from it.
            e.alias_label = (widen::form(e.inferred_type) == widen::Type::Form::kAlias)
                ? widen::spell(e.inferred_type)
                : tree.entries[e.resolved_entry_id].alias_label;
            return;
        }
        case parse::Kind::kCallExpr: {
            // Pick the overload (if any) + infer args; then the call yields its
            // return type (widening into `context` happens at codegen, like an
            // ident read). Reject a void return used where a value is wanted.
            classifyCall(tree, e, diag);
            if (e.return_type == widen::intern("void")) {
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
            inferExpr(tree, operand, widen::kNoType, diag);
            std::string ot = widen::spellOrEmpty(operand.inferred_type);
            if (isReference(operand.inferred_type)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Arithmetic is not allowed on a reference.", {}});
            } else if (isIteratorType(operand.inferred_type)) {
                // ok — an iterator steps by one element.
            } else if (!ot.empty()
                       && (!isNumericType(operand.inferred_type)
                           || widen::deepStrip(operand.inferred_type)
                                  == widen::intern("bool"))) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Operator '" + e.text + "' is not defined on type '"
                    + ot + "'.", {}});
            }
            // The result type IS the operand's — keep its handle, don't re-intern
            // the spelling (that would clobber an alias operand to a slid).
            e.inferred_type = operand.inferred_type;
            return;
        }
        case parse::Kind::kUnaryExpr: {
            assert(e.children.size() == 1 && "UnaryExpr needs 1 child");
            parse::Node& operand = *e.children[0];
            std::string const& op = e.text;
            if (op == "!") {
                inferExpr(tree, operand, widen::kNoType, diag);
                if (operand.inferred_type != widen::kNoType
                    && !isCoercibleToBool(operand.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '!' is not defined on type '"
                        + widen::spellOrEmpty(operand.inferred_type) + "'.", {}});
                }
                e.inferred_type = widen::intern("bool");
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
                inferExpr(tree, lhs, widen::kNoType, diag);
                inferExpr(tree, rhs, widen::kNoType, diag);
                if (lhs.inferred_type != widen::kNoType
                    && !isCoercibleToBool(lhs.inferred_type)) {
                    diagnostic::report(diag, {lhs.file_id, lhs.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + widen::spellOrEmpty(lhs.inferred_type) + "'.", {}});
                }
                if (rhs.inferred_type != widen::kNoType
                    && !isCoercibleToBool(rhs.inferred_type)) {
                    diagnostic::report(diag, {rhs.file_id, rhs.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + widen::spellOrEmpty(rhs.inferred_type) + "'.", {}});
                }
                e.inferred_type = widen::intern("bool");
                e.op_type = widen::intern("bool");
                return;
            }

            if (op == "<<" || op == ">>") {
                inferExpr(tree, lhs, context, diag);
                // Shift count stands alone — flexing into a float lhs would
                // mis-type a small int literal. Codegen handles width mismatch.
                inferExpr(tree, rhs, widen::kNoType, diag);
                if (lhs.inferred_type != widen::kNoType
                    && !isNumericType(lhs.inferred_type)) {
                    diagnostic::report(diag, {lhs.file_id, lhs.tok,
                        "Shift left-hand side must be numeric; got '"
                        + widen::spellOrEmpty(lhs.inferred_type) + "'.", {}});
                }
                if (rhs.inferred_type != widen::kNoType
                    && !isIntegerClass(rhs.inferred_type)) {
                    diagnostic::report(diag, {rhs.file_id, rhs.tok,
                        "Shift count must be integer-class; got '"
                        + widen::spellOrEmpty(rhs.inferred_type) + "'.", {}});
                }
                e.inferred_type = lhs.inferred_type;
                e.op_type = lhs.inferred_type;
                e.alias_label = lhs.alias_label;   // a shift keeps the lhs label
                return;
            }

            // Comparison and arith/bitwise: infer each side without context,
            // then literal-flex, then commonType.
            inferExpr(tree, lhs, widen::kNoType, diag);
            inferExpr(tree, rhs, widen::kNoType, diag);
            widen::TypeRef lref = lhs.inferred_type, rref = rhs.inferred_type;

            // Tuple operand(s): slot-wise arithmetic, with a scalar operand
            // BROADCAST to every slot. Arith / bitwise only (comparison / logical
            // on a tuple is not defined here). Result = the per-slot common type
            // tuple. (Nested-tuple slots are deferred — a non-scalar slot fails
            // the per-slot commonType.)
            {
                bool ltup = widen::form(widen::strip(lref)) == widen::Type::Form::kTuple;
                bool rtup = widen::form(widen::strip(rref)) == widen::Type::Form::kTuple;
                if (ltup || rtup) {
                    bool is_cmp = (op == "==" || op == "!=" || op == "<"
                                || op == "<=" || op == ">"  || op == ">=");
                    if (is_cmp) {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "Operator '" + op + "' is not defined on a tuple.", {}});
                        return;
                    }
                    std::vector<widen::TypeRef> lslots, rslots;
                    if (ltup) lslots = widen::get(widen::strip(lref)).slots;
                    if (rtup) rslots = widen::get(widen::strip(rref)).slots;
                    if (ltup && rtup && lslots.size() != rslots.size()) {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "Tuple shapes differ: '" + widen::spellOrEmpty(lref)
                            + "' vs '" + widen::spellOrEmpty(rref) + "'.", {}});
                        return;
                    }
                    std::size_t n = ltup ? lslots.size() : rslots.size();
                    // Flex a literal scalar into the (homogeneous) slot type so it
                    // doesn't default to a wider int width than the tuple's slots.
                    if (!ltup && n > 0) {
                        inferExpr(tree, lhs, rslots[0], diag);
                        lref = lhs.inferred_type;
                    }
                    if (!rtup && n > 0) {
                        inferExpr(tree, rhs, lslots[0], diag);
                        rref = rhs.inferred_type;
                    }
                    std::vector<widen::TypeRef> out_slots;
                    for (std::size_t i = 0; i < n; i++) {
                        widen::TypeRef ls = ltup ? lslots[i] : lref;
                        widen::TypeRef rs = rtup ? rslots[i] : rref;
                        widen::TypeRef ctr;
                        if (!widen::commonType(ls, rs, ctr)) {
                            diagnostic::report(diag, {e.file_id, e.tok,
                                "No common type for tuple slot "
                                + std::to_string(i) + " ('" + widen::spellOrEmpty(ls)
                                + "' and '" + widen::spellOrEmpty(rs) + "').", {}});
                            return;
                        }
                        if ((op == "&" || op == "|" || op == "^")
                            && isFloatType(ctr)) {
                            diagnostic::report(diag, {e.file_id, e.tok,
                                "Bitwise '" + op
                                + "' not defined on a floating-point slot.", {}});
                            return;
                        }
                        out_slots.push_back(ctr);
                    }
                    e.inferred_type = widen::internTuple(out_slots);
                    e.op_type = e.inferred_type;
                    return;
                }
            }

            // Pointer operands (reference / iterator / nullptr): the six
            // relational ops compare them (same pointee type required; nullptr
            // exempt), and arithmetic is rejected. Handled before the numeric
            // commonType path, which has no notion of pointer types.
            if (isPtrLikeType(lref) || isPtrLikeType(rref)) {
                bool is_cmp = (op == "==" || op == "!=" || op == "<"
                            || op == "<=" || op == ">" || op == ">=");
                if (!is_cmp) {
                    // References admit no arithmetic at all.
                    if (isReference(lref) || isReference(rref)) {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "Arithmetic is not allowed on a reference.", {}});
                        return;
                    }
                    // Iterators step by element: `iter ± int` -> iterator;
                    // `iter - iter` (same pointee) -> intptr (element count).
                    bool lit = isIteratorType(lref);
                    bool rit = isIteratorType(rref);
                    if (op == "+"
                        && ((lit && isIntegerClass(rref))
                            || (rit && isIntegerClass(lref)))) {
                        e.inferred_type = lit ? lref : rref;
                        e.op_type = e.inferred_type;
                        return;
                    }
                    if (op == "-" && lit && isIntegerClass(rref)) {
                        e.inferred_type = lref;
                        e.op_type = lref;
                        return;
                    }
                    if (op == "-" && lit && rit) {
                        if (widen::deepStrip(pointeeType(lref)) != widen::deepStrip(pointeeType(rref))) {
                            diagnostic::report(diag, {e.file_id, e.tok,
                                "Pointer subtraction requires the same pointee "
                                "type.", {}});
                            return;
                        }
                        e.inferred_type = widen::intern("intptr");
                        e.op_type = lref;   // element stride for codegen
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
                if (ordering && (isReference(lref) || isReference(rref))) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "References support only '==' and '!=' comparison.",
                        {}});
                    return;
                }
                widen::TypeRef anyptr = widen::intern("anyptr");
                bool lnull = lref == anyptr;
                bool rnull = rref == anyptr;
                if (!lnull && !rnull && widen::deepStrip(pointeeType(lref)) != widen::deepStrip(pointeeType(rref))) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Pointer comparison requires the same pointee type.",
                        {}});
                    return;
                }
                e.inferred_type = widen::intern("bool");
                e.op_type = lnull ? rref : lref;
                return;
            }

            flexBinaryOperands(lhs, rhs);
            std::string lt = widen::spellOrEmpty(lhs.inferred_type);   // for the message
            std::string rt = widen::spellOrEmpty(rhs.inferred_type);   // (keeps alias name)

            widen::TypeRef optyRef;
            if (!widen::commonType(lhs.inferred_type, rhs.inferred_type, optyRef)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "No common type for '" + lt + "' and '"
                    + rt + "'; use an explicit type conversion.",
                    {}});
                return;
            }
            if ((op == "&" || op == "|" || op == "^") && isFloatType(optyRef)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Bitwise '" + op + "' not defined on floating-point type '"
                    + widen::spellOrEmpty(optyRef) + "'.", {}});
                return;
            }

            bool is_cmp = (op == "==" || op == "!=" || op == "<"
                        || op == "<=" || op == ">"  || op == ">=");
            e.inferred_type = is_cmp ? widen::intern("bool") : optyRef;
            e.op_type = optyRef;
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
        case parse::Kind::kMoveStmt:
        case parse::Kind::kSwapStmt:
        case parse::Kind::kDestructureStmt:
        case parse::Kind::kDeleteStmt:
        case parse::Kind::kDtorCallStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kMethodCallStmt:
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
        case parse::Kind::kForArrayStmt:
        case parse::Kind::kForTupleStmt:
        case parse::Kind::kForRangedStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kSwitchStmt:
        case parse::Kind::kCaseClause:
        case parse::Kind::kParam:
        case parse::Kind::kClassDef:
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
            widen::TypeRef destRef = (i < s.param_types.size())
                ? s.param_types[i] : widen::kNoType;
            inferExpr(tree, *s.children[i], destRef, diag);
            widen::TypeRef byref = autoRefPointee(destRef, *s.children[i]);
            checkValueAssign(tree, byref != widen::kNoType ? byref : destRef,
                             *s.children[i], diag);
        }
        return;
    }
    // 2+ overloads: infer the PROVIDED args without context, then rank by cost
    // (a candidate is viable if its arity range admits this arg count and every
    // provided arg converts; omitted trailing optionals are filled from defaults).
    for (auto& ch : s.children) {
        if (ch) inferExpr(tree, *ch, widen::kNoType, diag);
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
            int c = s.children[i]
                ? argConvertCost(*s.children[i], widen::strip(e.param_types[i]))
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
        if (i < s.param_types.size()) {
            widen::TypeRef byref = autoRefPointee(s.param_types[i], *s.children[i]);
            checkValueAssign(tree, byref != widen::kNoType ? byref : s.param_types[i],
                             *s.children[i], diag);
        }
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
void checkPtrAssign(widen::TypeRef lvalue_type, parse::Node const& rhs,
                    diagnostic::Sink& diag) {
    // An empty type on either side rides an already-reported upstream error
    // (an unresolved name, a void value, ...) — skip silently rather than emit a
    // misleading second diagnostic. Deliberate, not a missing check.
    if (rhs.inferred_type == widen::kNoType || lvalue_type == widen::kNoType) return;
    if (!isPtrLikeType(lvalue_type) && !isPtrLikeType(rhs.inferred_type)) return;  // numeric
    if (ptrImplicitOk(rhs.inferred_type, lvalue_type)) return;
    diagnostic::report(diag, {rhs.file_id, rhs.tok,
        "Cannot implicitly cast '" + widen::spellOrEmpty(rhs.inferred_type)
        + "' to '" + widen::spellOrEmpty(lvalue_type)
        + "'; an explicit cast is required.", {}});
}

// A class (kSlid) VALUE is assignable only to the SAME class. A class meeting an
// unrelated type — a primitive, or a different class — has no conversion (codegen
// would otherwise store a struct into a scalar slot). This is the terminal reject
// the assign/decl dispatch otherwise lacks: pointer cases are checkPtrAssign's,
// same-class is a fine copy, and two non-classes flex/widen per codegen's numeric
// rules — so only a class-vs-mismatch falls through to here, the silent default
// that let `int y = classVal` compile. (A class LVALUE at a decl is constructed
// via classifyClassInit upstream and never reaches this dispatch.)
void checkSlidAssign(widen::TypeRef lvalue_type, parse::Node const& rhs,
                     diagnostic::Sink& diag) {
    if (rhs.inferred_type == widen::kNoType || lvalue_type == widen::kNoType) return;
    if (isPtrLikeType(lvalue_type) || isPtrLikeType(rhs.inferred_type)) return;
    using F = widen::Type::Form;
    widen::TypeRef l = widen::strip(lvalue_type);
    widen::TypeRef r = widen::strip(rhs.inferred_type);
    if (widen::form(l) != F::kSlid && widen::form(r) != F::kSlid) return;
    if (l == r) return;   // same class -> a copy
    diagnostic::report(diag, {rhs.file_id, rhs.tok,
        "Cannot implicitly convert '" + widen::spellOrEmpty(rhs.inferred_type)
        + "' to '" + widen::spellOrEmpty(lvalue_type) + "'.", {}});
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
void checkStrongConstAssign(widen::TypeRef dest, parse::Node const& rhs,
                            diagnostic::Sink& diag) {
    if (rhs.strong_type == widen::kNoType || !isLiteralKind(rhs.kind)
        || dest == widen::kNoType) return;
    std::string src = widen::spellOrEmpty(rhs.strong_type);
    std::string dest_s = widen::spellOrEmpty(dest);
    widen::TypeKind st, dt;
    if (!widen::classify(rhs.strong_type, st) || !widen::classify(dest, dt)) return;
    if (st.cat == dt.cat && st.bits == dt.bits) return;          // same type
    using C = widen::Category;
    auto narrow = [&] {
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Cannot implicitly narrow '" + src + "' to '" + dest_s
            + "'; use an explicit type conversion.", {}});
    };
    auto convertErr = [&](char const* tail) {
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Cannot implicitly convert '" + src + "' to '" + dest_s + "' (" + tail
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
    // bool source / int<->float cross-family: left to checkValueWiden.
}

// Typed-value narrowing / cross-family check — mirrors widen::convert's reject
// rules at CLASSIFY time, so codegen's widen::convert reduces to pure lowering
// + asserts. Type-only (no node — uses src/dest TypeRefs); the caret is the
// caller's (rhs's tok). Covers: signed→signed narrowing, unsigned→unsigned
// narrowing, unsigned→signed of same-or-narrower width, signed→unsigned (any
// width), float→float narrowing, bool→float, and the int↔float cross-family.
// Same-width-class and widening within family pass silently.
void checkValueWiden(widen::TypeRef dest, widen::TypeRef src,
                     int file_id, int tok, diagnostic::Sink& diag) {
    if (dest == widen::kNoType || src == widen::kNoType) return;
    widen::TypeKind st, dt;
    if (!widen::classify(src, st) || !widen::classify(dest, dt)) return;
    if (st.cat == dt.cat && st.bits == dt.bits) return;
    std::string s = widen::spellOrEmpty(src);
    std::string d = widen::spellOrEmpty(dest);
    using C = widen::Category;
    auto narrow = [&] {
        diagnostic::report(diag, {file_id, tok,
            "Cannot implicitly narrow '" + s + "' to '" + d
            + "'; use an explicit type conversion.", {}});
    };
    auto convertErr = [&](char const* tail) {
        diagnostic::report(diag, {file_id, tok,
            "Cannot implicitly convert '" + s + "' to '" + d + "' (" + tail
            + "); use an explicit type conversion.", {}});
    };
    auto convertErrPlain = [&] {
        diagnostic::report(diag, {file_id, tok,
            "Cannot implicitly convert '" + s + "' to '" + d
            + "'; use an explicit type conversion.", {}});
    };
    if (st.cat == C::kBool) {
        // bool widens to int family (zext); cross-family to float.
        if (dt.cat == C::kFloat) convertErrPlain();
        return;
    }
    if (st.cat == C::kSignedInt && dt.cat == C::kSignedInt) {
        if (dt.bits <= st.bits) narrow();
        return;
    }
    if (st.cat == C::kUnsignedInt && dt.cat == C::kUnsignedInt) {
        if (dt.bits <= st.bits) narrow();
        return;
    }
    if (st.cat == C::kUnsignedInt && dt.cat == C::kSignedInt) {
        if (dt.bits <= st.bits) convertErr("unsigned to same-width signed");
        return;
    }
    if (st.cat == C::kSignedInt && dt.cat == C::kUnsignedInt) {
        convertErr("signed \xe2\x86\x92 unsigned");
        return;
    }
    if (st.cat == C::kFloat && dt.cat == C::kFloat) {
        if (dt.bits <= st.bits) narrow();
        return;
    }
    // int↔float cross-family.
    convertErrPlain();
}

// An array initialized / assigned from a tuple LITERAL: `int a[3] = (1,2,3)`,
// `a = (4,5,6)`, `int td[2][3] = ((1,2),(3,4),(5,6))`. (A tuple value rhs is not
// handled here — it falls to the normal path and is rejected.)
bool isArrayFromTuple(widen::TypeRef declType, parse::Node const& rhs) {
    return widen::form(widen::strip(declType)) == widen::Type::Form::kArray
        && rhs.kind == parse::Kind::kTupleExpr;
}

// Collect the array's ELEMENT initializer nodes from a (possibly nested) tuple
// literal, descending exactly dims.size() levels of nesting — the ARRAY dims. At
// that depth each node is ONE element, taken WHOLE: a scalar for a primitive
// element, or a tuple for a tuple element (an array OF tuples is NOT flattened
// further — each element stays an aggregate). Returns false if the nesting doesn't
// match the declared dims (a wrong child count, or a transposed / flat literal),
// enforcing the row × col shape (standard order: dims[0] is the outermost).
bool collectArrayElementNodes(parse::Node& n, std::vector<int> const& dims,
                              std::size_t i, std::vector<parse::Node*>& out) {
    if (i == dims.size()) {           // element depth — take this node whole
        out.push_back(&n);
        return true;
    }
    if (n.kind != parse::Kind::kTupleExpr) return false;
    if (static_cast<int>(n.children.size()) != dims[i]) return false;
    for (auto& c : n.children) {
        if (!c || !collectArrayElementNodes(*c, dims, i + 1, out)) return false;
    }
    return true;
}

// Validate (and type) an array-from-tuple init/assign: the literal's NESTING must
// match the declared dimensions, and each dims-deep node initializes one element.
void classifyArrayFromTuple(parse::Tree& tree, widen::TypeRef declType,
                            parse::Node& rhs, diagnostic::Sink& diag) {
    widen::TypeRef a = widen::strip(declType);
    widen::TypeRef elem = widen::get(a).elem;
    std::vector<int> const& dims = widen::get(a).dims;
    std::vector<parse::Node*> elems;
    if (!collectArrayElementNodes(rhs, dims, 0, elems)) {
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Array initializer shape does not match the dimensions of '"
            + widen::spellOrEmpty(declType) + "'.", {}});
        return;
    }
    // Each node initializes ONE element — re-infer in the element's context (a
    // literal flexes into it), then validate `a[i] = el`. A SCALAR element obeys
    // the pointer-cast / strong-const rules (typed narrowing is codegen's convert);
    // a TUPLE element must match the element tuple type (slot-wise, after flex).
    bool elem_array = widen::form(widen::strip(elem)) == widen::Type::Form::kArray;
    bool elem_tuple = widen::form(widen::strip(elem)) == widen::Type::Form::kTuple;
    widen::TypeRef elem_s = widen::strip(elem);
    bool elem_class = widen::form(elem_s) == widen::Type::Form::kSlid
        && tree.classes.count(elem_s) > 0;
    for (parse::Node* el : elems) {
        // A CLASS element is CONSTRUCTED from its init value (a scalar / tuple is
        // the element's ctor input), recursively — exactly like a class-typed
        // field. Rewrite the element node in place to the construction tuple.
        if (elem_class) {
            parse::ClassInfo const& sub = tree.classes.at(elem_s);
            int el_file = el->file_id, el_tok = el->tok;
            auto init = std::make_unique<parse::Node>(std::move(*el));
            auto built = constructClass(tree, sub, std::move(init),
                                        el_file, el_tok, diag);
            *el = std::move(*built);
            el->inferred_type = elem;
            continue;
        }
        // A nested ARRAY element is itself an array-from-tuple — validate it as such
        // and type it as the array (so construction builds an array value).
        if (elem_array && el->kind == parse::Kind::kTupleExpr) {
            classifyArrayFromTuple(tree, elem, *el, diag);
            el->inferred_type = elem;
            continue;
        }
        inferExpr(tree, *el, elem, diag);
        if (elem_tuple || elem_array) {
            // A non-literal aggregate (tuple / array) VALUE element must match the
            // element type (an array literal was routed through the recursion above).
            if (el->inferred_type != widen::kNoType
                && widen::deepStrip(el->inferred_type) != widen::deepStrip(elem)) {
                diagnostic::report(diag, {el->file_id, el->tok,
                    "Array element type '" + widen::spellOrEmpty(el->inferred_type)
                    + "' does not match the declared element type '"
                    + widen::spellOrEmpty(elem) + "'.", {}});
            }
        } else {
            // A SCALAR element re-enters the relation against the element type.
            checkValueAssign(tree, elem, *el, diag);
        }
    }
}

// A tuple's flattened scalar-slot count (nested tuples expand recursively).
long tupleFlatSlotCount(widen::TypeRef t) {
    widen::Type const& ty = widen::get(widen::strip(t));
    if (ty.form != widen::Type::Form::kTuple) return 1;
    long n = 0;
    for (widen::TypeRef s : ty.slots) n += tupleFlatSlotCount(s);
    return n;
}

// An array initialized / assigned from a tuple VALUE — a tuple-typed expression
// that is NOT a literal (`int a[4] = t4;`). The literal case is isArrayFromTuple
// (its leaf NODES are re-inferred); a value has no node-level leaves, so it copies
// through the aggregate (codegen extractvalue per slot).
bool isArrayFromTupleValue(widen::TypeRef declType, parse::Node const& rhs) {
    return widen::form(widen::strip(declType)) == widen::Type::Form::kArray
        && rhs.kind != parse::Kind::kTupleExpr
        && widen::form(widen::strip(rhs.inferred_type)) == widen::Type::Form::kTuple;
}

// Validate an array-from-tuple-VALUE init/assign: the tuple's flattened slot count
// must equal the array's element count. Per-slot widening into the element type is
// checked + emitted by codegen (widen::convert), as for any typed-value assign.
void classifyArrayFromTupleValue(widen::TypeRef declType, parse::Node& rhs,
                                 diagnostic::Sink& diag) {
    widen::TypeRef a = widen::strip(declType);
    long total = 1;
    for (int d : widen::get(a).dims) total *= d;
    long slots = tupleFlatSlotCount(rhs.inferred_type);
    if (slots != total) {
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Tuple has " + std::to_string(slots)
            + " elements but array '" + widen::spellOrEmpty(declType)
            + "' has " + std::to_string(total) + ".", {}});
    }
}

// A tuple initialized / assigned from an array VALUE — the mirror of
// isArrayFromTupleValue (`(int,int,int,int) t = a1;`). A tuple lhs taking an
// array-typed rhs; copies through the aggregate (codegen extractvalue per element
// in row-major order, storing into the tuple's slots).
bool isTupleFromArrayValue(widen::TypeRef declType, parse::Node const& rhs) {
    return widen::form(widen::strip(declType)) == widen::Type::Form::kTuple
        && widen::form(widen::strip(rhs.inferred_type)) == widen::Type::Form::kArray;
}

// Validate a tuple-from-array-VALUE init/assign: the array's element count must
// equal the tuple's flattened slot count. Per-slot widening of the element type
// into the slot type is checked + emitted by codegen (widen::convert).
void classifyTupleFromArrayValue(widen::TypeRef declType, parse::Node& rhs,
                                 diagnostic::Sink& diag) {
    widen::TypeRef arr = widen::strip(rhs.inferred_type);
    long total = 1;
    for (int d : widen::get(arr).dims) total *= d;
    long slots = tupleFlatSlotCount(declType);
    if (slots != total) {
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Array '" + widen::spellOrEmpty(rhs.inferred_type)
            + "' has " + std::to_string(total) + " elements but tuple '"
            + widen::spellOrEmpty(declType) + "' has "
            + std::to_string(slots) + ".", {}});
    }
}

// An array VALUE rhs — a whole array, or a partial-index SUB-ARRAY slice
// (`a6[2]`) — assigns by whole-value copy, so the target must be the SAME array
// type. Returns true if the rhs was array-typed (handled here: a match is silent,
// a mismatch reported); false if the rhs isn't an array (the caller falls through
// to the pointer / strong-const checks). Run AFTER the array<->tuple arms so a
// tuple source is routed there first.
// Validate aggregate-to-aggregate assignment SHAPE (dims / arity) at every
// composite level. Per-element TYPE COMPATIBILITY (widen / narrow / cross-
// family) is left to codegen's per-element widen::convert, which reports any
// leaf-level rejection at the rhs's caret. Mirrors checkConvertCompat's walk
// shape; the leaf op differs (codegen widen::convert vs classify diagnostic).
void checkAggregateShapeMatch(widen::TypeRef dest, widen::TypeRef src,
                              parse::Node const& rhs, diagnostic::Sink& diag) {
    using F = widen::Type::Form;
    widen::TypeRef ds = widen::strip(dest);
    widen::TypeRef ss = widen::strip(src);
    F df = widen::form(ds);
    F sf = widen::form(ss);
    std::string to = widen::spellOrEmpty(dest);
    std::string from = widen::spellOrEmpty(src);
    if (df == F::kArray || sf == F::kArray) {
        if (df != F::kArray || sf != F::kArray) {
            diagnostic::report(diag, {rhs.file_id, rhs.tok,
                "Cannot assign '" + from + "' to '" + to + "'.", {}});
            return;
        }
        std::vector<int> const& ddims = widen::get(ds).dims;
        std::vector<int> const& sdims = widen::get(ss).dims;
        if (ddims != sdims) {
            diagnostic::report(diag, {rhs.file_id, rhs.tok,
                "Cannot assign '" + from + "' to '" + to
                + "'; array shape differs.", {}});
            return;
        }
        checkAggregateShapeMatch(widen::get(ds).elem, widen::get(ss).elem,
                                 rhs, diag);
        return;
    }
    if (df == F::kTuple || sf == F::kTuple) {
        if (df != F::kTuple || sf != F::kTuple) {
            diagnostic::report(diag, {rhs.file_id, rhs.tok,
                "Cannot assign '" + from + "' to '" + to + "'.", {}});
            return;
        }
        auto const& dslots = widen::get(ds).slots;
        auto const& sslots = widen::get(ss).slots;
        if (dslots.size() != sslots.size()) {
            diagnostic::report(diag, {rhs.file_id, rhs.tok,
                "Cannot assign '" + from + "' to '" + to + "'; slot count differs ("
                + std::to_string(sslots.size()) + " vs "
                + std::to_string(dslots.size()) + ").", {}});
            return;
        }
        for (std::size_t i = 0; i < dslots.size(); i++) {
            checkAggregateShapeMatch(dslots[i], sslots[i], rhs, diag);
        }
        return;
    }
    // Leaf: typed-value widen check at the classify level (so codegen's
    // widen::convert reduces to pure lowering + asserts). Pointer-like / class
    // leaves are covered by checkPtrAssign / checkSlidAssign at the top level
    // — the recursive aggregate walk here only handles numeric leaves; a
    // pointer/class slot in an aggregate would have failed earlier checks.
    if (!isPtrLikeType(dest) && !isPtrLikeType(src)) {
        checkValueWiden(dest, src, rhs.file_id, rhs.tok, diag);
    }
}

bool checkArrayValueAssign(widen::TypeRef dest, parse::Node const& rhs,
                           diagnostic::Sink& diag) {
    if (widen::form(widen::strip(rhs.inferred_type)) != widen::Type::Form::kArray)
        return false;
    checkAggregateShapeMatch(dest, rhs.inferred_type, rhs, diag);
    return true;
}

// Deep-clone an expression subtree (a field default reused at a construction
// site). parse::Node holds unique_ptr children, so it can't be copy-constructed.
std::unique_ptr<parse::Node> cloneExpr(parse::Node const& n) {
    auto c = std::make_unique<parse::Node>();
    c->kind = n.kind;
    c->name = n.name;
    c->text = n.text;
    c->return_type = n.return_type;
    c->nominal_type = n.nominal_type;
    c->inferred_type = n.inferred_type;
    c->op_type = n.op_type;
    c->alias_label = n.alias_label;
    c->strong_type = n.strong_type;
    c->file_id = n.file_id;
    c->tok = n.tok;
    c->name_tok = n.name_tok;
    c->resolved_entry_id = n.resolved_entry_id;
    c->qualifier = n.qualifier;
    c->qualifier_toks = n.qualifier_toks;
    c->global_qualified = n.global_qualified;
    for (auto const& ch : n.children) {
        c->children.push_back(ch ? cloneExpr(*ch) : nullptr);
    }
    return c;
}

// A zero-valued node for a field with no init slot and no author default: the
// "appropriate zero value" — 0 / 0.0 / false / nullptr / (for a class field) a
// recursively default-constructed tuple.
std::unique_ptr<parse::Node> classZeroValue(parse::Tree& tree, widen::TypeRef ty,
                                            int file_id, int tok,
                                            diagnostic::Sink& diag);

// Normalize a class var-decl initializer into a per-field construction tuple:
// each field takes its init slot (left to right), else the author default, else
// zero. The result is a kTupleExpr typed as the class; codegen fills the struct.
void classifyClassInit(parse::Tree& tree, parse::Node& s,
                       parse::ClassInfo const& info, diagnostic::Sink& diag);

// Build a fully-constructed value for class `info` from an optional init value
// (a scalar / tuple / same-class value), filling the field defaults / zeros.
// Returns the construction tuple (typed as the class). Mutually recursive with
// classifyClassInit (a class-typed field constructs its sub-class the same way).
std::unique_ptr<parse::Node> constructClass(parse::Tree& tree,
                                            parse::ClassInfo const& info,
                                            std::unique_ptr<parse::Node> init,
                                            int file_id, int tok,
                                            diagnostic::Sink& diag) {
    auto holder = std::make_unique<parse::Node>();
    holder->kind = parse::Kind::kVarDeclStmt;
    holder->file_id = file_id;
    holder->tok = tok;
    if (init) holder->children.push_back(std::move(init));
    classifyClassInit(tree, *holder, info, diag);
    return std::move(holder->children[0]);
}

void classifyClassInit(parse::Tree& tree, parse::Node& s,
                       parse::ClassInfo const& info, diagnostic::Sink& diag) {
    std::size_t n = info.field_names.size();
    std::vector<std::unique_ptr<parse::Node>> provided;
    if (!s.children.empty() && s.children[0]) {
        parse::Node& init = *s.children[0];
        if (init.kind == parse::Kind::kTupleExpr) {
            for (auto& ch : init.children) provided.push_back(std::move(ch));
        } else {
            provided.push_back(std::move(s.children[0]));
        }
    }
    if (provided.size() > n) {
        // Caret the first EXTRA initializer (the offender), not the decl.
        parse::Node const& extra = *provided[n];
        diagnostic::report(diag, {extra.file_id, extra.tok,
            "Class '" + info.name + "' has " + std::to_string(n)
            + " field(s) but " + std::to_string(provided.size())
            + " initializer(s) were given.", {}});
        provided.resize(n);
    }
    auto tup = std::make_unique<parse::Node>();
    tup->kind = parse::Kind::kTupleExpr;
    tup->file_id = s.file_id;
    tup->tok = s.tok;
    for (std::size_t i = 0; i < n; i++) {
        widen::TypeRef ft = info.field_types[i];
        widen::TypeRef fts = widen::strip(ft);
        // Read the author default LIVE off the stable kParam node (constfold may
        // have replaced the default expression in place since resolve).
        parse::Node* fparam = info.field_params[i];
        parse::Node* fdefault = (fparam && !fparam->children.empty())
            ? fparam->children[0].get() : nullptr;
        std::unique_ptr<parse::Node> provVal;
        if (i < provided.size() && provided[i]) provVal = std::move(provided[i]);

        // A CLASS field is constructed (not raw-filled): a value already of the
        // field's class type is a copy; a scalar / tuple is the field's
        // constructor input, recursively filled with the sub-class's defaults; no
        // value default-constructs it.
        if (widen::form(fts) == widen::Type::Form::kSlid
            && tree.classes.count(fts)) {
            parse::ClassInfo const& sub = tree.classes.at(fts);
            std::unique_ptr<parse::Node> slot;
            if (provVal) {
                inferExpr(tree, *provVal, ft, diag);
                if (widen::deepStrip(provVal->inferred_type) == widen::deepStrip(ft)) {
                    slot = std::move(provVal);   // same-class value -> copy
                } else {
                    slot = constructClass(tree, sub, std::move(provVal),
                                          s.file_id, s.tok, diag);
                }
            } else if (fdefault) {
                slot = constructClass(tree, sub, cloneExpr(*fdefault),
                                      s.file_id, s.tok, diag);
            } else {
                slot = constructClass(tree, sub, nullptr, s.file_id, s.tok, diag);
            }
            tup->children.push_back(std::move(slot));
            continue;
        }

        // A non-class field: provided slot / author default / zero.
        std::unique_ptr<parse::Node> slot;
        if (provVal) slot = std::move(provVal);
        else if (fdefault) slot = cloneExpr(*fdefault);
        else slot = classZeroValue(tree, ft, s.file_id, s.tok, diag);
        inferExpr(tree, *slot, ft, diag);
        // The field's init obeys the full assignment relation (pointer, strong-const,
        // class, and the aggregate-vs-scalar / array / tuple gates).
        checkValueAssign(tree, ft, *slot, diag);
        tup->children.push_back(std::move(slot));
    }
    tup->inferred_type = info.type;
    s.children.clear();
    s.children.push_back(std::move(tup));
}

std::unique_ptr<parse::Node> classZeroValue(parse::Tree& tree, widen::TypeRef ty,
                                            int file_id, int tok,
                                            diagnostic::Sink& diag) {
    widen::TypeRef st = widen::strip(ty);
    widen::Type const& t = widen::get(st);
    auto n = std::make_unique<parse::Node>();
    n->file_id = file_id;
    n->tok = tok;
    using F = widen::Type::Form;
    if (t.form == F::kSlid) {
        // A class-typed field with no value -> recursively default-construct it.
        auto it = tree.classes.find(st);
        if (it != tree.classes.end()) {
            classifyClassInit(tree, *n, it->second, diag);
            // classifyClassInit set n->children to [tuple]; lift the tuple up.
            auto tuple = std::move(n->children[0]);
            return tuple;
        }
        // An unregistered class — unreachable (resolve validated the field type),
        // so fall through to the diagnostic rather than silently zeroing.
    } else if (t.form == F::kPointer || t.form == F::kIterator
               || t.form == F::kAnyptr) {
        n->kind = parse::Kind::kNullptrLiteral;
        return n;
    } else if (t.form == F::kPrimitive) {
        if (t.cat == widen::Category::kFloat) {
            n->kind = parse::Kind::kFloatLiteral;
            n->text = "0.0";
            return n;
        }
        if (t.cat == widen::Category::kBool) {
            n->kind = parse::Kind::kBoolLiteral;
            n->text = "false";
            return n;
        }
        // signed / unsigned integer (incl. char) -> 0
        n->kind = parse::Kind::kIntLiteral;
        n->text = "0";
        return n;
    }
    // Any other field type (array, tuple, void, an unregistered class) has no
    // defined zero here — report rather than silently emit an int 0.
    diagnostic::report(diag, {file_id, tok,
        "Cannot default-construct a field of type '" + widen::spell(ty)
        + "'.", {}});
    n->kind = parse::Kind::kIntLiteral;   // placeholder; the error short-circuits codegen
    n->text = "0";
    return n;
}

// The ONE assignment relation: may a source VALUE (`rhs`, already inferred by the
// caller) flow into a target SLOT of type `dest`, and with what implicit
// conversion? This is the canonical chain — the cross-shape array<->tuple arms, the
// array/tuple VALUE gates, then the pointer / strong-const / class terminal checks —
// run at EVERY assignment-family site (decl-init, assign, store, move, call/method
// args, return, field-init, per-element). inferExpr is the caller's job, BEFORE
// this; the helper is the post-inference check only. (Swap and class CONSTRUCTION
// sit outside the relation — they do not call this.)
void checkValueAssign(parse::Tree& tree, widen::TypeRef dest, parse::Node& rhs,
                      diagnostic::Sink& diag) {
    if (dest == widen::kNoType) return;
    if (isArrayFromTuple(dest, rhs)) {
        classifyArrayFromTuple(tree, dest, rhs, diag);
    } else if (isArrayFromTupleValue(dest, rhs)) {
        classifyArrayFromTupleValue(dest, rhs, diag);
    } else if (isTupleFromArrayValue(dest, rhs)) {
        classifyTupleFromArrayValue(dest, rhs, diag);
    } else if (checkArrayValueAssign(dest, rhs, diag)) {
        // array VALUE rhs — handled (matched, or mismatch reported).
    } else {
        // No cross-shape arm fired and the rhs is not an array value. A tuple/array
        // DEST, or a tuple VALUE rhs, that reaches here is an aggregate-vs-scalar or
        // tuple-vs-tuple case the arms do not cover — gate it before the scalar /
        // pointer / class checks, which would silently accept the mismatch.
        auto isAgg = [](widen::TypeRef t) {
            widen::Type::Form f = widen::form(widen::strip(t));
            return f == widen::Type::Form::kTuple || f == widen::Type::Form::kArray;
        };
        bool destAgg = isAgg(dest);
        bool rhsAgg = isAgg(rhs.inferred_type);
        if (destAgg && rhsAgg) {
            // Both tuple/array; the arms leave only tuple <- tuple. A tuple LITERAL
            // already flexed its slots into `dest` via inferExpr (leave it). A
            // tuple VALUE needs SHAPE match — codegen's per-element widen::convert
            // handles leaf compat (widen / narrow / cross-family).
            if (rhs.kind != parse::Kind::kTupleExpr
                && rhs.inferred_type != widen::kNoType) {
                checkAggregateShapeMatch(dest, rhs.inferred_type, rhs, diag);
            }
        } else if (destAgg != rhsAgg) {
            // A tuple/array on one side, a scalar / pointer / class on the other:
            // no conversion exists (codegen would store a struct into a scalar slot,
            // or vice-versa). A kNoType rhs rides an already-reported upstream error.
            if (rhs.inferred_type != widen::kNoType) {
                diagnostic::report(diag, {rhs.file_id, rhs.tok,
                    "Cannot assign '" + widen::spellOrEmpty(rhs.inferred_type)
                    + "' to '" + widen::spellOrEmpty(dest) + "'.", {}});
            }
        } else {
            // Neither side is a tuple/array: scalar / pointer / class. The terminal
            // checks — pointer implicit-cast, strong-const widen, typed-value widen,
            // class same-type.
            checkPtrAssign(dest, rhs, diag);
            checkStrongConstAssign(dest, rhs, diag);
            if (!isLiteralKind(rhs.kind)
                && !isPtrLikeType(rhs.inferred_type)
                && !isPtrLikeType(dest)) {
                checkValueWiden(dest, rhs.inferred_type,
                                rhs.file_id, rhs.tok, diag);
            }
            checkSlidAssign(dest, rhs, diag);
        }
    }
}

// True when two move/swap operands name the SAME variable — both are bare
// identifier references (kIdentExpr) resolved to the same entry. A self-swap is
// a no-op and a self-move would null the source it just copied from, so both are
// rejected. Indexed / deref self (`arr[i] <--> arr[i]`, `p^ <-- p^`) needs
// structural lvalue-equality and is left to a later pass.
bool isSameLvalue(parse::Node const& a, parse::Node const& b) {
    return a.kind == parse::Kind::kIdentExpr
        && b.kind == parse::Kind::kIdentExpr
        && a.resolved_entry_id >= 0
        && a.resolved_entry_id == b.resolved_entry_id;
}

// The element types a tuple or array destructures into: a tuple's slots, or N copies
// of an array's (sub-)element type. Empty if `t` is neither (not destructurable).
std::vector<widen::TypeRef> destructureSlots(widen::TypeRef t) {
    widen::TypeRef s = widen::strip(t);
    if (widen::form(s) == widen::Type::Form::kTuple) return widen::get(s).slots;
    if (widen::form(s) == widen::Type::Form::kArray) {
        widen::Type const& at = widen::get(s);
        widen::TypeRef elem = (at.dims.size() <= 1) ? at.elem
            : widen::internArray(at.elem,
                  std::vector<int>(at.dims.begin() + 1, at.dims.end()));
        return std::vector<widen::TypeRef>(at.dims.front(), elem);
    }
    return {};
}

// Type-check a destructure's slots (node.children[1..]) against `slots` (the element
// types of the source `src`). A typeless DECLARED slot takes its element's type; a
// typed slot or a REUSED variable must match it; a NESTED slot recurses (its element
// must itself be a tuple/array); null is a discard.
void classifyDestructureSlots(parse::Tree& tree, parse::Node& node,
                              std::vector<widen::TypeRef> const& slots,
                              widen::TypeRef src, diagnostic::Sink& diag) {
    std::size_t ntargets = node.children.size() - 1;
    if (ntargets != slots.size()) {
        diagnostic::report(diag, {node.file_id, node.tok,
            "Destructure has " + std::to_string(ntargets)
            + " target(s) but the tuple '" + widen::spellOrEmpty(src) + "' has "
            + std::to_string(slots.size()) + " slot(s).", {}});
        return;
    }
    for (std::size_t i = 0; i < ntargets; i++) {
        parse::Node* slot = node.children[i + 1].get();
        if (!slot) continue;   // discard
        widen::TypeRef slot_ty = slots[i];
        if (slot->kind == parse::Kind::kDestructureStmt) {
            std::vector<widen::TypeRef> sub = destructureSlots(slot_ty);
            if (sub.empty()) {
                diagnostic::report(diag, {slot->file_id, slot->tok,
                    "A nested destructure target needs a tuple or array element; got '"
                    + widen::spellOrEmpty(slot_ty) + "'.", {}});
                continue;
            }
            classifyDestructureSlots(tree, *slot, sub, slot_ty, diag);
            continue;
        }
        if (slot->kind == parse::Kind::kVarDeclStmt
            && slot->return_type == widen::kNoType) {
            slot->return_type = slot_ty;
            if (slot->resolved_entry_id >= 0)
                tree.entries[slot->resolved_entry_id].slids_type = slot_ty;
        } else {
            widen::TypeRef have = (slot->kind == parse::Kind::kVarDeclStmt)
                ? slot->return_type
                : parse::entryType(tree, slot->resolved_entry_id);
            if (have != widen::kNoType
                && widen::deepStrip(have) != widen::deepStrip(slot_ty)) {
                diagnostic::report(diag, {slot->file_id, slot->name_tok,
                    "Destructure target '" + slot->name + "' has type '"
                    + widen::spellOrEmpty(have) + "' but slot " + std::to_string(i)
                    + " is '" + widen::spellOrEmpty(slot_ty) + "'.", {}});
            }
        }
    }
}

void classifyStmt(parse::Tree& tree, parse::Node& s,
                  widen::TypeRef fn_return_type,
                  diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            // A class-typed local is constructed: normalize the init (slot /
            // default / zero) into a typed construction tuple. The pointer /
            // array / strong-const init rules below don't apply.
            if (widen::form(widen::strip(s.return_type)) == widen::Type::Form::kSlid) {
                auto it = tree.classes.find(widen::strip(s.return_type));
                if (it != tree.classes.end()) {
                    classifyClassInit(tree, s, it->second, diag);
                    return;
                }
            }
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
                if (s.return_type == widen::kNoType && s.resolved_entry_id >= 0
                    && !s.is_const) {
                    parse::Node& rhs = *s.children[0];
                    // Invariant: a typeable rhs yields a non-empty inferred type.
                    // An un-typeable one (namespace/type-as-value, no-common-type,
                    // void call) reports a diagnostic first and main short-circuits
                    // before codegen — so an empty type here only ever coexists
                    // with an already-reported error (don't abort on that path).
                    assert((diagnostic::hasErrors(diag)
                            || rhs.inferred_type != widen::kNoType)
                        && "inferred-init: a typeable rhs must yield a type");
                    // rhs.inferred_type is ALREADY the right handle: a weak literal's
                    // is the preferred no-width default (defaultLiteralType returns
                    // int/uint/float, never int32), and a typed rhs keeps its exact
                    // handle (alias/structured types survive). A STRONG literal
                    // (substituted from a typed const, e.g. `const int8 K`) keeps its
                    // declared width — its strong_type IS its type. No spell->intern.
                    widen::TypeRef inferred =
                        (isLiteralKind(rhs.kind) && rhs.strong_type != widen::kNoType)
                            ? rhs.strong_type
                            : rhs.inferred_type;
                    s.return_type = inferred;
                    tree.entries[s.resolved_entry_id].slids_type = inferred;
                    tree.entries[s.resolved_entry_id].alias_label = rhs.alias_label;
                }
                // A for-tuple LITERAL spill temp must be homogeneous — the loop
                // iterates by an iterator strided by slot 0's type, so a mixed
                // tuple would misread the other slots. (A tuple VARIABLE is checked
                // at resolve, where its type is known.)
                if (s.require_homogeneous
                    && widen::form(widen::strip(s.return_type))
                           == widen::Type::Form::kTuple) {
                    std::vector<widen::TypeRef> slots =
                        widen::get(widen::strip(s.return_type)).slots;
                    for (std::size_t i = 1; i < slots.size(); i++) {
                        if (widen::deepStrip(slots[i]) != widen::deepStrip(slots[0])) {
                            diagnostic::report(diag, {s.file_id, s.tok,
                                "A for-loop over a tuple requires a homogeneous "
                                "tuple.", {}});
                            break;
                        }
                    }
                }
                // The initializer obeys the assignment relation (a typeless init
                // took the rhs type above, so its dest is kNoType and the helper is
                // a no-op).
                checkValueAssign(tree, s.return_type, *s.children[0], diag);
            }
            return;
        }
        case parse::Kind::kAssignStmt: {
            // resolve already stamped resolved_entry_id (or emitted an error
            // and we won't reach here — main short-circuits on resolve errors).
            std::string lvalue_type;
            widen::TypeRef lref = widen::kNoType;
            if (s.resolved_entry_id >= 0) {
                lref = parse::entryType(tree, s.resolved_entry_id);
                lvalue_type = widen::spellOrEmpty(lref);
            }
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, lref, diag);
            }
            // The rhs obeys the assignment relation against the lvalue type.
            if (!s.children.empty() && s.children[0]) {
                checkValueAssign(tree, lref, *s.children[0], diag);
            }
            return;
        }
        case parse::Kind::kAugAssignStmt: {
            // resolve stamped resolved_entry_id and cached lvalue type as
            // s.return_type. If resolve failed, we shouldn't be here (main
            // short-circuits) — but be defensive and walk rhs anyway.
            std::string const& op = s.text;
            parse::Node* rhs_ptr;
            if (s.children.size() == 2) {
                // Complex lvalue form: [0]=lvalue chain, [1]=rhs. Infer the lvalue
                // and cache its type as return_type — the checks below + desugar
                // read it exactly like the bare-name entry type.
                parse::Node& lv = *s.children[0];
                inferExpr(tree, lv, widen::kNoType, diag);
                s.return_type = lv.inferred_type;
                rhs_ptr = s.children[1].get();
            } else {
                // Bare-name form. Refresh the lvalue type from the entry: an
                // inferred-init local was still untyped when resolve cached
                // s.return_type here, but classify stamped it when it ran the
                // (promoted) decl above. desugar reads s.return_type, so update it
                // in place. For an ordinary local this is the value resolve cached.
                if (s.resolved_entry_id >= 0) {
                    s.return_type = parse::entryType(tree, s.resolved_entry_id);
                }
                rhs_ptr = s.children[0].get();
            }
            parse::Node& rhs = *rhs_ptr;
            std::string lvalue_type = widen::spellOrEmpty(s.return_type);

            // A reference admits no compound arithmetic/bitwise assignment.
            if (isReference(s.return_type)) {
                inferExpr(tree, rhs, widen::kNoType, diag);
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Arithmetic is not allowed on a reference.", {}});
                return;
            }

            // An iterator steps by element: `iter += n` / `iter -= n` only (the
            // compound form of `iter ± int`). Any other compound op on a pointer
            // is rejected like the binary path. Desugar lowers this to
            // `iter = iter + n`, whose GEP + assignment already handle it.
            if (isIteratorType(s.return_type)) {
                inferExpr(tree, rhs, widen::kNoType, diag);
                if ((op == "+" || op == "-")
                    && isIntegerClass(rhs.inferred_type)) {
                    s.inferred_type = s.return_type;
                    s.op_type = s.return_type;
                    return;
                }
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Arithmetic is not defined on a pointer.", {}});
                return;
            }

            if (op == "<<" || op == ">>") {
                inferExpr(tree, rhs, widen::kNoType, diag);
                if (!isNumericType(s.return_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Shift left-hand side must be numeric; got '"
                        + lvalue_type + "'.", {}});
                }
                if (rhs.inferred_type != widen::kNoType
                    && !isIntegerClass(rhs.inferred_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Shift count must be integer-class; got '"
                        + widen::spellOrEmpty(rhs.inferred_type) + "'.", {}});
                }
                s.inferred_type = s.return_type;
                s.op_type = s.return_type;
                return;
            }
            if (op == "&&" || op == "||" || op == "^^") {
                inferExpr(tree, rhs, widen::kNoType, diag);
                if (!isCoercibleToBool(s.return_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + lvalue_type + "'.", {}});
                }
                if (rhs.inferred_type != widen::kNoType
                    && !isCoercibleToBool(rhs.inferred_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Operator '" + op + "' is not defined on type '"
                        + widen::spellOrEmpty(rhs.inferred_type) + "'.", {}});
                }
                s.inferred_type = widen::intern("bool");
                s.op_type = widen::intern("bool");
                return;
            }
            // arith / bitwise — rhs literal flexes into lvalue's type, then
            // commonType drives the op.
            inferExpr(tree, rhs, s.return_type, diag);
            widen::TypeRef optyRef;
            if (!widen::commonType(s.return_type, rhs.inferred_type, optyRef)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "No common type for '" + lvalue_type + "' and '"
                    + widen::spellOrEmpty(rhs.inferred_type)
                    + "'; use an explicit type conversion.", {}});
                return;
            }
            if ((op == "&" || op == "|" || op == "^") && isFloatType(optyRef)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Bitwise '" + op + "' not defined on floating-point type '"
                    + widen::spellOrEmpty(optyRef) + "'.", {}});
                return;
            }
            s.inferred_type = optyRef;
            s.op_type = optyRef;
            // The binary's result stores back into the lvalue. If the common
            // type widened beyond the lvalue's width (`int16 += int32`), the
            // store narrows — catch at classify so widen::convert can assert.
            if (!isPtrLikeType(s.return_type) && !isPtrLikeType(optyRef)) {
                checkValueWiden(s.return_type, optyRef,
                                s.file_id, s.tok, diag);
            }
            return;
        }
        case parse::Kind::kStoreStmt: {
            // `lvalue^ = rhs` (deref) or `arr[i] = rhs` (index). Infer the lvalue
            // (-> the pointee / element type), then infer the rhs in that context
            // so a literal flexes to it.
            assert(s.children.size() == 2 && "kStoreStmt needs lvalue + rhs");
            parse::Node& lvalue = *s.children[0];
            inferExpr(tree, lvalue, widen::kNoType, diag);
            inferExpr(tree, *s.children[1], lvalue.inferred_type, diag);
            // The stored rhs obeys the assignment relation against the pointee /
            // element type (a SUB-ARRAY target like `grid[1] = (7,8)` takes a
            // tuple/array source element-wise; a pointer-typed slot obeys the
            // implicit-cast rules; a strong-const literal the widen rules).
            checkValueAssign(tree, lvalue.inferred_type, *s.children[1], diag);
            return;
        }
        case parse::Kind::kMoveStmt: {
            // `a <-- b;` — the COPY obeys assignment rules (widen for values,
            // implicit pointer-cast for pointers; desugar adds the source-nulling
            // afterward). Infer the lhs lvalue, then the rhs in that context so a
            // literal flexes, then check the same pointer / strong-const rules a
            // store would. The null step needs no check (nulling a pointer is
            // always valid).
            assert(s.children.size() == 2 && "kMoveStmt needs lhs + rhs");
            parse::Node& lhs = *s.children[0];
            inferExpr(tree, lhs, widen::kNoType, diag);
            inferExpr(tree, *s.children[1], lhs.inferred_type, diag);
            // A self-move would null the source it just copied from — reject it.
            if (isSameLvalue(lhs, *s.children[1])) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Cannot move a value onto itself.", {}});
                return;
            }
            // The COPY half of the move obeys the assignment relation.
            checkValueAssign(tree, lhs.inferred_type, *s.children[1], diag);
            return;
        }
        case parse::Kind::kSwapStmt: {
            // `a <--> b;` — exchange; both operands must be EXACTLY the same type
            // (no widening — a symmetric swap cannot convert both directions).
            assert(s.children.size() == 2 && "kSwapStmt needs two operands");
            parse::Node& a = *s.children[0];
            parse::Node& b = *s.children[1];
            inferExpr(tree, a, widen::kNoType, diag);
            inferExpr(tree, b, widen::kNoType, diag);
            // A self-swap is a no-op and almost certainly a bug — reject it (the
            // same variable trivially passes the same-type check below).
            if (isSameLvalue(a, b)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Cannot swap a value with itself.", {}});
                return;
            }
            // Both-non-kNoType guard is cascade suppression: a kNoType operand
            // means inferExpr already reported an error, so skip the mismatch
            // report to avoid a second misleading diagnostic. user notified,
            // accepts state.
            if (a.inferred_type != widen::kNoType && b.inferred_type != widen::kNoType
                && widen::deepStrip(a.inferred_type) != widen::deepStrip(b.inferred_type)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Swap operands must be the same type; got '"
                    + widen::spellOrEmpty(a.inferred_type) + "' and '"
                    + widen::spellOrEmpty(b.inferred_type) + "'.", {}});
            }
            return;
        }
        case parse::Kind::kDestructureStmt: {
            // children[0] = rhs (a TUPLE or ARRAY — a homogeneous tuple), [1..] =
            // declarator / nested slots. Shape + per-slot checks recurse for nested.
            parse::Node& rhs = *s.children[0];
            inferExpr(tree, rhs, widen::kNoType, diag);
            std::vector<widen::TypeRef> slots = destructureSlots(rhs.inferred_type);
            if (slots.empty()) {
                if (rhs.inferred_type != widen::kNoType)
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "The right side of a destructure must be a tuple or array; "
                        "got '" + widen::spellOrEmpty(rhs.inferred_type) + "'.", {}});
                return;
            }
            classifyDestructureSlots(tree, s, slots, rhs.inferred_type, diag);
            return;
        }
        case parse::Kind::kDeleteStmt: {
            // delete p; — p must be a pointer (reference / iterator). resolve
            // already checked it is a variable lvalue.
            parse::Node& operand = *s.children[0];
            inferExpr(tree, operand, widen::kNoType, diag);
            if (operand.inferred_type != widen::kNoType
                && !isPtrLikeType(operand.inferred_type)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Cannot delete a non-pointer value of type '"
                    + widen::spellOrEmpty(operand.inferred_type) + "'.", {}});
            }
            return;
        }
        case parse::Kind::kDtorCallStmt: {
            // lvalue.~(); — the receiver must be a CLASS lvalue (the object whose
            // dtor runs). resolve resolved it; type it and require a class.
            parse::Node& recv = *s.children[0];
            inferExpr(tree, recv, widen::kNoType, diag);
            widen::TypeRef rs = widen::strip(recv.inferred_type);
            if (recv.inferred_type != widen::kNoType
                && !(widen::form(rs) == widen::Type::Form::kSlid
                     && tree.classes.count(rs) > 0)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A destructor call '.~()' requires a class object; got '"
                    + widen::spellOrEmpty(recv.inferred_type) + "'.", {}});
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
        case parse::Kind::kMethodCallStmt: {
            // obj.method(args) — infer the receiver, resolve the method on its
            // class's member frame, arity-check + infer the args. desugar reads the
            // receiver's class off children[0].inferred_type to mint the symbol.
            parse::Node& recv = *s.children[0];
            inferExpr(tree, recv, widen::kNoType, diag);
            if (recv.inferred_type == widen::kNoType) return;
            widen::TypeRef rs = widen::strip(recv.inferred_type);
            if (!(widen::form(rs) == widen::Type::Form::kSlid
                  && tree.classes.count(rs) > 0)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A method call requires a class object; got '"
                    + widen::spellOrEmpty(recv.inferred_type) + "'.", {}});
                return;
            }
            // Resolve the method as a member of the receiver's class frame (shared
            // member lookup). A same-name NON-method member is not a method.
            int classId = parse::classEntryForType(tree, rs);
            int methodId = classId < 0 ? -1
                : parse::findMemberDeclared(tree, tree.entries[classId].ns_frame_id, s.name);
            if (methodId < 0
                || tree.entries[methodId].kind != parse::EntryKind::kFunction) {
                diagnostic::report(diag, {s.file_id, s.name_tok,
                    "Class '" + widen::spell(rs) + "' has no method '" + s.name + "'.", {}});
                return;
            }
            // Thread the resolved method onto the node — desugar mints the symbol
            // from the method's OWN defining class, not the receiver's (so a base
            // method called on a derived receiver still names the base symbol).
            s.resolved_entry_id = methodId;
            parse::Entry const& m = tree.entries[methodId];
            // param_types = [self, user...]; children = [receiver, args...]. The
            // user-arg count is param_types.size()-1, and arg children[i] aligns
            // with param_types[i] (i>=1) — same shape the lowered call emits.
            std::size_t nargs = s.children.size() - 1;
            std::size_t nuser = m.param_types.empty() ? 0 : m.param_types.size() - 1;
            if (nargs != nuser) {
                diagnostic::report(diag, {s.file_id, s.name_tok,
                    "Method '" + s.name + "' expects " + std::to_string(nuser)
                    + " arguments, got " + std::to_string(nargs) + ".", {}});
                return;
            }
            for (std::size_t i = 1; i < s.children.size(); i++) {
                if (!s.children[i]) continue;
                // Type-check each arg against its param, like classifyCall — infer
                // with the param as context, then reject a const / class / convert
                // mismatch (else a wrong-typed arg passed silently).
                inferExpr(tree, *s.children[i], m.param_types[i], diag);
                widen::TypeRef byref = autoRefPointee(m.param_types[i], *s.children[i]);
                checkValueAssign(tree, byref != widen::kNoType ? byref : m.param_types[i],
                                 *s.children[i], diag);
            }
            s.param_types = m.param_types;    // [self, user...] — emitCall arity check
            s.return_type = m.slids_type;     // emitCall reads return_type
            s.inferred_type = m.slids_type;
            return;
        }
        case parse::Kind::kReturnStmt: {
            // A void function returning a value would emit `ret void <val>`
            // (invalid IR). Reject at the `return`. (Non-literal values would
            // otherwise slip past the literal-fit check into codegen.)
            if (fn_return_type == widen::intern("void") && !s.children.empty()) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A void function cannot return a value.", {}});
                return;
            }
            // A bare `return;` (no value) is only valid in a void function.
            if (fn_return_type != widen::intern("void") && s.children.empty()) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A non-void function must return a value.", {}});
                return;
            }
            for (auto& ch : s.children) {
                if (ch) {
                    inferExpr(tree, *ch, fn_return_type, diag);
                    checkValueAssign(tree, fn_return_type, *ch, diag);
                }
            }
            return;
        }
        case parse::Kind::kExprStmt: {
            // value discarded — infer for its checks (lvalue / numeric) only.
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, widen::kNoType, diag);
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
            inferExpr(tree, cond, widen::kNoType, diag);
            if (cond.inferred_type != widen::kNoType
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "An if condition must be a condition expression; type '"
                    + widen::spellOrEmpty(cond.inferred_type) + "' is not.", {}});
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
            inferExpr(tree, cond, widen::kNoType, diag);
            if (cond.inferred_type != widen::kNoType
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A while condition must be a condition expression; type '"
                    + widen::spellOrEmpty(cond.inferred_type) + "' is not.", {}});
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
            inferExpr(tree, cond, widen::kNoType, diag);
            if (cond.inferred_type != widen::kNoType
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A while condition must be a condition expression; type '"
                    + widen::spellOrEmpty(cond.inferred_type) + "' is not.", {}});
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);
            return;
        }
        case parse::Kind::kForLongStmt: {
            // children[0]=cond, [1]=update, [2]=body, [3..]=varlist decls.
            assert(s.children.size() >= 3 && "kForLongStmt needs cond+update+body");
            // The loop var [3] is classified first so a typeless (inferred) loop
            // var has its type stamped before the rest of the varlist (a later
            // varlist decl may reference it).
            if (s.children.size() > 3 && s.children[3]) {
                classifyStmt(tree, *s.children[3], fn_return_type, diag);
            }
            for (std::size_t i = 4; i < s.children.size(); i++) {
                if (s.children[i]) classifyStmt(tree, *s.children[i], fn_return_type, diag);
            }
            parse::Node& cond = *s.children[0];
            inferExpr(tree, cond, widen::kNoType, diag);
            if (cond.inferred_type != widen::kNoType
                && !isCoercibleToBool(cond.inferred_type)) {
                diagnostic::report(diag, {cond.file_id, cond.tok,
                    "A for condition must be a condition expression; type '"
                    + widen::spellOrEmpty(cond.inferred_type) + "' is not.", {}});
            }
            classifyStmt(tree, *s.children[1], fn_return_type, diag);   // update
            classifyStmt(tree, *s.children[2], fn_return_type, diag);   // body
            return;
        }
        case parse::Kind::kForRangedStmt: {
            // [0]=loop-var decl (init=start), [1]=end, [2]=step|null, [3]=body.
            // text=cmp. Type the loop var first, then flow its type into the bound
            // and step so they coerce to it (matching the kForLongStmt path, where
            // the synthesized `_$end`/`_$step` inherit the loop var's type).
            assert(s.children.size() == 4
                && "kForRangedStmt needs var+end+step+body");
            classifyStmt(tree, *s.children[0], fn_return_type, diag);
            widen::TypeRef lvt = widen::kNoType;
            if (s.children[0]->resolved_entry_id >= 0) {
                lvt = parse::entryType(tree, s.children[0]->resolved_entry_id);
            }
            inferExpr(tree, *s.children[1], lvt, diag);
            if (s.children[2]) inferExpr(tree, *s.children[2], lvt, diag);
            // Bound + step must coerce to the loop-var type at codegen; catch
            // narrowing at classify so widen::convert can assert there.
            if (lvt != widen::kNoType && !isPtrLikeType(lvt)) {
                if (s.children[1]->inferred_type != widen::kNoType
                    && !isPtrLikeType(s.children[1]->inferred_type)) {
                    checkValueWiden(lvt, s.children[1]->inferred_type,
                                    s.children[1]->file_id, s.children[1]->tok, diag);
                }
                if (s.children[2]
                    && s.children[2]->inferred_type != widen::kNoType
                    && !isPtrLikeType(s.children[2]->inferred_type)) {
                    checkValueWiden(lvt, s.children[2]->inferred_type,
                                    s.children[2]->file_id, s.children[2]->tok, diag);
                }
            }
            classifyStmt(tree, *s.children[3], fn_return_type, diag);
            // Empty-range check: both bounds constant and `start cmp end` false ->
            // the body never runs. start = the loop-var init; end = children[1].
            if (s.range_dotdot_tok >= 0 && !s.children[0]->children.empty()
                && rangeFirstTestFalse(*s.children[0]->children[0],
                                       *s.children[1], s.text)) {
                diagnostic::report(diag, {s.file_id, s.range_dotdot_tok,
                    "Invalid range.", {}});
            }
            return;
        }
        case parse::Kind::kForArrayStmt: {
            // [0]=loop-var decl (typed in resolve, no init), [1]=array ident,
            // [2]=body. The element binding + `_$idx` counter are built in desugar;
            // here only the iterable's array type needs stamping (desugar reads its
            // element / length) and the body classified with the loop var in scope.
            assert(s.children.size() == 3 && "kForArrayStmt needs var+array+body");
            inferExpr(tree, *s.children[1], widen::kNoType, diag);
            // BY-VALUE loop var that's too narrow for the element type narrows
            // at each iteration's binding — catch at classify so widen::convert
            // can assert.
            widen::TypeRef lv_t = widen::kNoType;
            if (s.children[0]->resolved_entry_id >= 0) {
                lv_t = parse::entryType(tree, s.children[0]->resolved_entry_id);
            }
            if (lv_t != widen::kNoType && !isPtrLikeType(lv_t)) {
                widen::TypeRef arr_t = s.children[1]->inferred_type;
                if (widen::form(widen::strip(arr_t))
                        == widen::Type::Form::kArray) {
                    widen::TypeRef elem_t = widen::get(widen::strip(arr_t)).elem;
                    checkValueWiden(lv_t, elem_t,
                                    s.children[0]->file_id,
                                    s.children[0]->tok, diag);
                }
            }
            classifyStmt(tree, *s.children[2], fn_return_type, diag);
            return;
        }
        case parse::Kind::kForTupleStmt: {
            // [0]=loop-var decl (no init), [1]=iterable, [2]=body. Type the
            // iterable (a literal flexes its slots to the loop var's element type),
            // check slot homogeneity, and give a FRESH typeless loop var the
            // element (slot 0) type. The `_$iter`/binding are built in desugar.
            assert(s.children.size() == 3 && "kForTupleStmt needs var+iterable+body");
            parse::Node& var_decl = *s.children[0];
            parse::Node& it_ref = *s.children[1];
            bool by_ref = widen::form(widen::strip(var_decl.return_type))
                          == widen::Type::Form::kPointer;
            widen::TypeRef context = widen::kNoType;
            if (var_decl.return_type != widen::kNoType
                && it_ref.kind == parse::Kind::kTupleExpr) {
                widen::TypeRef velem = by_ref
                    ? widen::get(widen::strip(var_decl.return_type)).pointee
                    : var_decl.return_type;
                int n = static_cast<int>(it_ref.children.size());
                context = widen::internTuple(std::vector<widen::TypeRef>(n, velem));
            }
            inferExpr(tree, it_ref, context, diag);
            widen::TypeRef itup = widen::strip(it_ref.inferred_type);
            // A tuple gives its slots directly; an ARRAY iterates as a homogeneous
            // tuple — N copies of the (N-1)-D sub-array (or base element for 1-D).
            std::vector<widen::TypeRef> slots;
            if (widen::form(itup) == widen::Type::Form::kTuple) {
                slots = widen::get(itup).slots;
            } else if (widen::form(itup) == widen::Type::Form::kArray) {
                widen::Type const& at = widen::get(itup);
                widen::TypeRef e = (at.dims.size() <= 1) ? at.elem
                    : widen::internArray(at.elem,
                        std::vector<int>(at.dims.begin() + 1, at.dims.end()));
                slots.assign(at.dims.front(), e);
            }
            // Both a tuple and an array (expression iterable) route here; name the
            // iterable correctly in diagnostics.
            char const* noun =
                (widen::form(itup) == widen::Type::Form::kArray) ? "array" : "tuple";
            if (!slots.empty()) {
                for (std::size_t i = 1; i < slots.size(); i++) {
                    if (widen::deepStrip(slots[i]) != widen::deepStrip(slots[0])) {
                        // (Homogeneity can only fail for a real tuple — an array's
                        // synthesized slots are identical.)
                        diagnostic::report(diag, {it_ref.file_id, it_ref.tok,
                            "A for-loop over a tuple requires a homogeneous tuple.",
                            {}});
                        break;
                    }
                }
                // A NON-PRIMITIVE element (a tuple / array / slid slot) can't be
                // bound by value here — it must be iterated by REFERENCE.
                widen::TypeRef elem = slots.empty() ? widen::kNoType : slots[0];
                widen::Type::Form ef = widen::form(widen::strip(elem));
                bool elem_aggregate = !slots.empty()
                    && (ef == widen::Type::Form::kTuple
                     || ef == widen::Type::Form::kArray
                     || ef == widen::Type::Form::kSlid);
                // A DECLARED by-value loop var over such an element is an error.
                if (elem_aggregate && var_decl.return_type != widen::kNoType
                    && !by_ref) {
                    diagnostic::report(diag, {var_decl.file_id, var_decl.name_tok,
                        std::string("A for-loop over a ") + noun + " with "
                        "non-primitive elements must use a reference loop variable.",
                        {}});
                }
                // An EXPLICITLY by-reference loop var (`T^ p`) must reference the
                // element type — mirrors the for-array by-ref check. (A typeless
                // var has kNoType, so by_ref is false; a forced-by-ref over a
                // non-primitive element matches by construction.) Without this, a
                // mismatch (`int^` over a char tuple) reached codegen as invalid IR.
                if (by_ref && !slots.empty()) {
                    widen::TypeRef pointee =
                        widen::get(widen::strip(var_decl.return_type)).pointee;
                    if (widen::strip(pointee) != widen::strip(elem)) {
                        diagnostic::report(diag, {var_decl.file_id, var_decl.name_tok,
                            "Loop variable type '"
                                + widen::spellOrEmpty(var_decl.return_type)
                                + "' does not match the " + noun + " element type '"
                                + widen::spell(elem) + "'.", {}});
                    }
                }
                // A FRESH typeless loop var takes the element (slot 0) type — a
                // non-primitive element FORCES a reference (`T^`). A reuse keeps its
                // enclosing type (the binding coerces the slot).
                if (var_decl.kind == parse::Kind::kVarDeclStmt
                    && var_decl.return_type == widen::kNoType
                    && var_decl.resolved_entry_id >= 0 && !slots.empty()) {
                    tree.entries[var_decl.resolved_entry_id].slids_type =
                        elem_aggregate ? widen::internPointer(elem) : elem;
                }
            } else if (it_ref.inferred_type != widen::kNoType) {
                // An unknown-typed iterable the dispatcher routed here on faith (a
                // typeless local — arrays are always typed — or an inferred-typed
                // expression, the only iterable expression form being a tuple) that
                // did NOT infer to a tuple: `x = 5; for (v : x)`, or an inferred ref
                // to a non-tuple. A named local names itself; an expression uses the
                // operand wording. (kNoType => an earlier error already fired.)
                if (it_ref.kind == parse::Kind::kIdentExpr) {
                    diagnostic::report(diag, {it_ref.file_id, it_ref.tok,
                        "'" + it_ref.name + "' is not an enum, array, or tuple.", {}});
                } else {
                    diagnostic::report(diag, {it_ref.file_id, it_ref.tok,
                        "A for-loop operand must be an enum, an array, or a tuple.",
                        {}});
                }
            }
            classifyStmt(tree, *s.children[2], fn_return_type, diag);
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
            inferExpr(tree, scrut, widen::kNoType, diag);
            std::string st = widen::spellOrEmpty(scrut.inferred_type);
            if (!st.empty() && !isIntegerClass(scrut.inferred_type)) {
                diagnostic::report(diag, {scrut.file_id, scrut.tok,
                    "A switch value must be integer-class; got '" + st + "'.",
                    {}});
            }
            std::map<long long, parse::Node const*> seen;   // value -> first label
            parse::Node const* first_default = nullptr;      // for the dup-default note
            for (std::size_t i = 1; i < s.children.size(); i++) {
                parse::Node& clause = *s.children[i];
                if (clause.children[0]) {
                    parse::Node& label = *clause.children[0];
                    inferExpr(tree, label, scrut.inferred_type, diag);
                    if (!isLiteralKind(label.kind)) {
                        diagnostic::report(diag, {label.file_id, label.tok,
                            "A case label must be a constant.", {}});
                    } else if (label.kind == parse::Kind::kFloatLiteral
                               || (label.inferred_type != widen::kNoType
                                   && !isIntegerClass(label.inferred_type))) {
                        diagnostic::report(diag, {label.file_id, label.tok,
                            "A case label must be an integer constant.", {}});
                    } else if (!st.empty() && !literalFitsContext(label, scrut.inferred_type)) {
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
                    if (first_default) {
                        diagnostic::report(diag, {clause.file_id, clause.tok,
                            "A switch may have only one default clause.",
                            {{first_default->file_id, first_default->tok,
                              "first default here"}}});
                    } else {
                        first_default = &clause;
                    }
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
        case parse::Kind::kClassDef:
            // A local class: type-check its ctor/dtor member bodies (self-bound;
            // field refs are already kFieldExpr from resolve), recursing into
            // hoisted classes. Construction lowers at the use site.
            classifyClassMemberBodies(tree, s, diag);
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
        case parse::Kind::kTupleExpr:
        case parse::Kind::kFieldExpr:
        case parse::Kind::kCastExpr:
        case parse::Kind::kConvertExpr:
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
        || s.kind == parse::Kind::kForArrayStmt || s.kind == parse::Kind::kForTupleStmt
        || s.kind == parse::Kind::kForRangedStmt
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
        if (p.return_type == widen::kNoType) {
            // def.inferred_type is ALREADY the preferred no-width handle: a no-context
            // literal default infers via defaultLiteralType (int / uint / float, never
            // int32). Use the handle directly — no spell->intern round-trip (which
            // would drop an alias label if a non-literal default ever lands here).
            // Mirrors the var-decl inferred-init path.
            widen::TypeRef t = def.inferred_type;
            p.return_type = t;
            e.param_types[i] = t;
            if (p.resolved_entry_id >= 0) {
                tree.entries[p.resolved_entry_id].slids_type = t;
            }
        } else if (!literalFitsContext(def, p.return_type)) {
            diagnostic::report(diag, {def.file_id, def.tok,
                "Default value does not fit parameter type '"
                    + widen::spellOrEmpty(p.return_type) + "'.", {}});
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
    if (fn.return_type != widen::intern("void")) {
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
                if (init) {
                    inferExpr(tree, *init, m->return_type, diag);
                    checkValueAssign(tree, m->return_type, *init, diag);
                }
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
            // Type-infer top-level const init in its declared type's context, then
            // run the one assignment relation — every assignment-family site does.
            for (auto& init : ch->children) {
                if (init) {
                    inferExpr(tree, *init, ch->return_type, diag);
                    checkValueAssign(tree, ch->return_type, *init, diag);
                }
            }
        } else if (ch->kind == parse::Kind::kClassDef) {
            // Type-check ctor/dtor member bodies (self-bound; field refs are
            // already kFieldExpr from resolve), recursing into hoisted classes.
            classifyClassMemberBodies(tree, *ch, diag);
        }
    }
}

}  // namespace classify
