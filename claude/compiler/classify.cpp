#include "classify.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "constfold.h"
#include "diagnostic.h"
#include "parse.h"
#include "resolve.h"
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

// A GLOBAL'S INITIALIZER IS DATA, AND DATA IS CONSTANT. A literal, or an aggregate of
// literals — nothing else. constfold has already run, so a constant scalar (folded
// arithmetic, a substituted const, an enum member) IS a literal node by now.
// A CONSTRUCTION EXPRESSION IS NOT DATA — `global Widget w = Widget(5);` is an error.
// It is tempting to admit it because its ARGUMENTS are constant, but a construction is
// exactly the thing that runs code, so admitting it lets the one expression form the
// rule exists to exclude back in. The DECLARATOR form `global Widget w(5);` remains the
// spelling for a class global with field values: `(5)` there is the FIELD LIST — data —
// not an rhs expression. `= 5` and `= (7, 9)` are the same fill.
// The class is never policed beyond that: its field DEFAULTS and its CTOR BODY are code
// and may do whatever they like. That is what the lazy first-touch gate is for, and it
// is where a global built from another global belongs — say it out loud, in a ctor:
// `global (Widget c) { _() { c = w_; } ~() {} }`.
// Rejecting a non-constant initializer is not a soundness fix (the gate orders
// cross-global reads correctly); it is language policy. An initializer that quietly
// reads another global looks like data and behaves like code.
bool isConstantInit(parse::Node const& e) {
    if (isLiteralKind(e.kind)) return true;
    if (e.kind == parse::Kind::kNullptrLiteral) return true;
    if (e.kind != parse::Kind::kTupleExpr) return false;
    for (auto const& c : e.children) {
        if (c && !isConstantInit(*c)) return false;
    }
    return true;
}

// A BARE LVALUE expression: one that names an existing storage location (an
// address), as opposed to a computed rvalue (a call / op / construction / literal).
// The distinction drives the copy-vs-elide and spread-vs-spill decisions: a bare
// lvalue can be addressed / re-indexed in place, so it is copied (op=) or spread
// directly; an rvalue must be built in place (elide) or spilled to a temp. Callers
// that additionally need SIDE-EFFECT freedom (safe to re-read per slot) test more
// narrowly (a bare kIdentExpr) — this is lvalue-ness, not evaluation-count safety.
bool isBareLvalue(parse::Node const& n) {
    parse::Kind k = n.kind;
    return k == parse::Kind::kIdentExpr
        || k == parse::Kind::kFieldExpr
        || k == parse::Kind::kIndexExpr
        || k == parse::Kind::kDerefExpr;
}

// The DEEP re-readable-lvalue test (defined below): may a source be re-read once per slot
// with no duplicated side effect? Forward-declared here for the class-field spread + spill.
bool isReReadableLvalue(parse::Node const& n);

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

// THE ARITHMETIC CONVENIENCE (canon widen.sl rule 1a): for the five
// arithmetic operators (+ - * / % and their aug-assign twins) an INTEGER
// operand silently converts to the FLOAT operand's type — the result is the
// float side's type, alias label intact. Everything else — comparisons
// (the 2^53 equality trap stays fenced), shifts, bitwise, logical — keeps
// the family wall: "No common type". bool stays excluded (its own kind);
// char rides as its implementation integer. Returns the float side's type,
// or kNoType when the rule does not apply.
widen::TypeRef arithFloatMix(widen::TypeRef l, widen::TypeRef r,
                             std::string const& op) {
    if (op != "+" && op != "-" && op != "*" && op != "/" && op != "%")
        return widen::kNoType;
    widen::TypeKind lk, rk;
    if (!widen::classify(widen::strip(l), lk)
        || !widen::classify(widen::strip(r), rk)) return widen::kNoType;
    auto isInt = [](widen::TypeKind const& k) {
        return k.cat == widen::Category::kSignedInt
            || k.cat == widen::Category::kUnsignedInt;
    };
    if (lk.cat == widen::Category::kFloat && isInt(rk)) return l;
    if (rk.cat == widen::Category::kFloat && isInt(lk)) return r;
    return widen::kNoType;
}

// The arithmetic convenience's LITERAL half: an integer literal operand in an
// admitted mix re-kinds as a WEAK float literal in place (decimal text is a
// valid float spelling), so downstream literal-widening never sees a
// cross-family ask. char/bool literals stay strict (not "integers").
void flexIntLiteralToFloat(parse::Node& n, widen::TypeRef float_side) {
    if (n.kind != parse::Kind::kIntLiteral
        && n.kind != parse::Kind::kUintLiteral) return;
    n.kind = parse::Kind::kFloatLiteral;
    n.strong_type = widen::kNoType;           // weak — the partner governs
    n.inferred_type = widen::strip(float_side);
}

// Spell a type for a diagnostic. An ALIAS spells as label AND target —
// 'T=int', 'Integer=int' — so a message like "No common type for 'T' and 'X'"
// says what the labels are actually bound to.
std::string spellForMessage(widen::TypeRef t) {
    if (t != widen::kNoType && widen::form(t) == widen::Type::Form::kAlias)
        return widen::spellOrEmpty(t) + "=" + widen::spellOrEmpty(widen::strip(t));
    return widen::spellOrEmpty(t);
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

// The number of FLAT initializers a class consumes during construction: a base
// (`_$base`) field splices its own fields in flat (recursively); every other field is
// one slot. So `C:B:A` (each adding one field) has flat width 3.
int flatFieldWidth(parse::Tree& tree, widen::TypeRef cls) {
    int w = 0;
    // Backstop only: a cyclic base chain is diagnosed by checkClassByValueAcyclic
    // (a base is a by-value `_$base` field); this guard just bounds the walk.
    int guard = (int)tree.classes.size() + 2;
    for (widen::TypeRef c = widen::strip(cls); guard-- > 0; ) {
        auto it = tree.classes.find(c);
        if (it == tree.classes.end()) { w += 1; break; }   // a non-class type is one slot
        parse::ClassInfo const& info = it->second;
        widen::TypeRef next = parse::baseTypeOf(info);      // only the base (slot 0) splices
        // The synthetic slot-0 field (`_$base` OR `_$vptr`) is never a construction slot.
        int synth0 = (next != widen::kNoType || parse::hasVptr(info)) ? 1 : 0;
        w += (int)info.field_names.size() - synth0;
        if (next == widen::kNoType) break;
        c = next;
    }
    return w;
}

// Is `base` a TRANSITIVE base of `derived`?
bool isTransitiveBase(parse::Tree& tree, widen::TypeRef base, widen::TypeRef derived) {
    base = widen::strip(base);
    for (widen::TypeRef b = parse::classBaseType(tree, derived);
         b != widen::kNoType; b = parse::classBaseType(tree, b)) {
        if (b == base) return true;
    }
    return false;
}

// A derived->base pointer is an IMPLICIT upcast (the base is at offset 0, so the
// pointer is unchanged): both pointer-ish, the target pointee a base of the source's.
bool ptrBaseUpcastOk(parse::Tree& tree, widen::TypeRef from, widen::TypeRef to) {
    if (!isPtrLikeType(from) || !isPtrLikeType(to)) return false;
    widen::TypeRef pf = castPointee(from), pt = castPointee(to);
    if (pf == widen::kNoType || pt == widen::kNoType) return false;
    return isTransitiveBase(tree, pt, pf);
}

// A base<->derived pointer cast is EXPLICITLY allowed (downcast or upcast); both at
// offset 0, so a value no-op.
bool ptrBaseCastOk(parse::Tree& tree, widen::TypeRef from, widen::TypeRef to) {
    if (!isPtrLikeType(from) || !isPtrLikeType(to)) return false;
    widen::TypeRef pf = castPointee(from), pt = castPointee(to);
    if (pf == widen::kNoType || pt == widen::kNoType) return false;
    return isTransitiveBase(tree, pf, pt) || isTransitiveBase(tree, pt, pf);
}

// A fixed-size array type (`int[5]`, `int[3][5]`), distinct from `int[]`
// (iterator) and `int^` (ref). strip() sees through a transparent alias.
bool isArrayType(widen::TypeRef t) {
    return widen::form(widen::strip(t)) == widen::Type::Form::kArray;
}

// The FIRST-level element type of an array — peel ONE dimension. `int[5]` -> int;
// `int[5][3]` -> int[3] (dims are source order, so drop dims[0]). kNoType if not an
// array. This is the pointee an array DECAYS to (`^arr[0]`).
widen::TypeRef arrayFirstElem(widen::TypeRef arr) {
    widen::Type const& a = widen::get(widen::strip(arr));
    if (a.form != widen::Type::Form::kArray) return widen::kNoType;
    widen::TypeRef elem = a.elem;                            // capture before intern
    if (a.dims.size() <= 1) return elem;
    std::vector<int> rest(a.dims.begin() + 1, a.dims.end());
    return widen::internArray(elem, rest);
}

// Rewrite an array node IN PLACE as its element address `^n[0]` — the array->pointer
// decay made explicit. An array is storage, not a pointer; its address is `^arr[0]`,
// an iterator over the element type. Codegen then handles an ordinary address-of, so
// no codegen change is needed; the CALLER re-infers. An `int^` target demotes the
// `Type[]` result to a reference through the normal iterator->reference rule.
void wrapArrayAsElemAddr(parse::Node& n) {
    // A STRING LITERAL is already an address at the IR level: it names N bytes in the
    // constant pool and codegen emits a pointer to them. There is no alloca to index and
    // no lvalue to take, so its decay is a pure TYPE change — re-stamp it as the element
    // pointer and leave the node alone. Wrapping it as `^lit[0]` would send codegen
    // looking for storage that does not exist. Guarded HERE, in the one decay funnel, so
    // every decay site inherits it.
    if (n.kind == parse::Kind::kStringLiteral) {
        n.inferred_type = widen::internIterator(widen::internConst(widen::intern("char")));
        return;
    }
    int fid = n.file_id, tk = n.tok;
    auto zero = std::make_unique<parse::Node>();
    zero->kind = parse::Kind::kIntLiteral;
    zero->text = "0";
    zero->file_id = fid; zero->tok = tk;
    auto idx = std::make_unique<parse::Node>();
    idx->kind = parse::Kind::kIndexExpr;
    idx->file_id = fid; idx->tok = tk;
    idx->children.push_back(std::make_unique<parse::Node>(std::move(n)));
    idx->children.push_back(std::move(zero));
    auto addr = std::make_unique<parse::Node>();
    addr->kind = parse::Kind::kAddrOfExpr;
    addr->file_id = fid; addr->tok = tk;
    addr->children.push_back(std::move(idx));
    n = std::move(*addr);
}

// An aggregate is an array OR a tuple — arrays are homogeneous tuples, and the
// two share one element-wise arithmetic path.
bool isAggregateType(widen::TypeRef t) {
    widen::Type::Form f = widen::form(widen::strip(t));
    return f == widen::Type::Form::kArray || f == widen::Type::Form::kTuple;
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

// Form-agnostic aggregate decomposition (an array is a homogeneous tuple): the
// number of top-level slots, and the i-th slot's type — a tuple's slot, or an
// array's element with the outermost dimension stripped.
int aggregateSlotCount(widen::TypeRef t) {
    widen::TypeRef s = widen::strip(t);
    if (widen::form(s) == widen::Type::Form::kTuple) {
        return static_cast<int>(widen::get(s).slots.size());
    }
    return arrayFirstDim(s);
}
widen::TypeRef aggregateSlotType(widen::TypeRef t, int i) {
    widen::TypeRef s = widen::strip(t);
    if (widen::form(s) == widen::Type::Form::kTuple) {
        return widen::get(s).slots[i];
    }
    return arrayElementType(s);
}

// The scalar leaf type of an aggregate — descend slot 0 / the element until a
// non-aggregate remains. Used to flex a broadcast scalar literal to an aggregate's
// element width (`int8[3] + 1` keeps int8 instead of widening to the literal int).
widen::TypeRef aggregateLeafType(widen::TypeRef t) {
    widen::TypeRef s = widen::strip(t);
    while (isAggregateType(s)) {
        s = widen::strip(aggregateSlotType(s, 0));
    }
    return s;
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

// True when `++`/`--` is defined on `t`: an iterator (steps one element), a
// numeric non-bool scalar (steps ±1), or an aggregate (tuple / array) every leaf
// of which is itself inc/dec-able (each leaf steps, recursively). A reference,
// pointer, bool, or any leaf failing these rejects. The per-leaf rule mirrors the
// scalar inc/dec arm so an aggregate is accepted exactly where `agg + 1` would be
// (plus iterator leaves, which step like a scalar iterator).
bool isIncDecable(widen::TypeRef t) {
    if (isReference(t)) return false;
    if (isIteratorType(t)) return true;
    if (isAggregateType(t)) {
        int n = aggregateSlotCount(t);
        for (int i = 0; i < n; i++) {
            if (!isIncDecable(aggregateSlotType(t, i))) return false;
        }
        return true;
    }
    return isNumericType(t) && widen::deepStrip(t) != widen::intern("bool");
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
    // An array IS a homogeneous tuple — convert array and tuple as the SAME shape at
    // every level (slot count + per-leaf convert), so a CROSS-FORM conversion
    // (`(int[2]=(1,2))`, `((int,int)=anIntArray)`) is accepted, mirroring the
    // form-agnostic array<->tuple VALUE assignment. The leaf still uses the explicit
    // value grid below (so narrowing is permitted, unlike an implicit assign).
    if (df == F::kTuple || df == F::kArray || sf == F::kTuple || sf == F::kArray) {
        bool dAgg = (df == F::kTuple || df == F::kArray);
        bool sAgg = (sf == F::kTuple || sf == F::kArray);
        if (dAgg != sAgg) {                          // aggregate vs scalar — no convert
            diagnostic::report(diag, {file_id, tok,
                "Cannot convert '" + from + "' to '" + to + "'.", {}});
            return true;
        }
        int dn = aggregateSlotCount(ds);
        int sn = aggregateSlotCount(ss);
        if (dn != sn) {
            diagnostic::report(diag, {file_id, tok,
                "Cannot convert '" + from + "' to '" + to + "'; slot count differs ("
                + std::to_string(sn) + " vs " + std::to_string(dn) + ").", {}});
            return true;
        }
        bool any = false;
        for (int i = 0; i < dn; i++) {
            widen::TypeRef dSlot = aggregateSlotType(ds, i);   // capture before recurse
            widen::TypeRef sSlot = aggregateSlotType(ss, i);
            if (checkConvertCompat(dSlot, sSlot, file_id, tok, diag)) any = true;
        }
        return any;
    }
    if (df == F::kSlid) {
        // A TOP-LEVEL class target is lowered in the kConvertExpr arm
        // (lowerClassConversion). Reaching here means a class leaf inside an
        // AGGREGATE slot — the aggregate value walk has no class-leaf arm, so
        // reject cleanly (not a supported conversion shape).
        diagnostic::report(diag, {file_id, tok,
            "Cannot convert to class type '" + to
            + "' inside an aggregate; a class conversion must be a top-level "
            "'(Class = src)'.", {}});
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
        case parse::Kind::kGlobalScopeStmt:
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
    if (widen::intLiteralFits(literalTextForFit(lit), context)) return true;
    // Upper-bits case: the value overflows context's magnitude, but the literal's
    // NOMINAL type still widens (e.g. ~0x0F is nominal uint8 -> flexes into int64,
    // its 0xFF..F0 value reinterpreting as -16). The materialization truncates the
    // bit pattern directly at codegen.
    return widen::nominalWidensTo(lit.nominal_type, context);
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
void classifyFunctionSignature(parse::Tree& tree, parse::Node& fn,
                               diagnostic::Sink& diag);
void classifyStmt(parse::Tree& tree, parse::Node& s,
                  widen::TypeRef fn_return_type, diagnostic::Sink& diag,
                  std::vector<std::unique_ptr<parse::Node>>* prelude);
// Resolve+type a method call (kMethodCallStmt): bind the method on the receiver's
// class, arity/type-check args, stamp param_types/return_type/inferred_type.
// Shared by the statement form (classifyStmt) and the expression form (inferExpr).
// A CONSTRUCTION receiver (`Class(a).m()`) is fine in either: desugar lifts it to a
// `_$cret` temp (liftSretCallExprs) whose address is passed as `_$recv`.
void inferMethodCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);
void lowerClassConversion(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag);
void lowerAggregateConversion(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag);
// THE SLOT-WISE EXPLODE (defined below, next to the spill funnel it stands on). The
// STATEMENT form (an aug-assign) is defined after classifyStmt, which it recurses into.
bool explodeAggregateExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag);
void explodeAggregateAug(parse::Tree& tree, parse::Node& s, widen::TypeRef fn_return_type,
                         diagnostic::Sink& diag, bool is_shift);
bool classHasOperatorArity(parse::Tree& tree, widen::TypeRef cls,
                           std::string const& opname, std::size_t nUserParams);
bool isArithBitBinaryOp(std::string const& op);
bool stampClassBinary(parse::Tree& tree, parse::Node& e, std::string const& op,
                      widen::TypeRef lref, diagnostic::Sink& diag);
// ONE-LEVEL OPERAND COERCION (defined next to findClassOperator, which it probes with).
bool coerceOperandToClass(parse::Tree& tree, widen::TypeRef cls,
                          parse::Node& operand, diagnostic::Sink& diag);

// The CLASS a parameter wants, seeing through a reference / iterator param (`Class^` — the
// form a non-primitive parameter is munged into), or kNoType when the param is not
// class-typed. The one decode of "does this position want a class?", shared by the call,
// method, and operator coercion retries.
widen::TypeRef classParamTarget(widen::TypeRef param) {
    widen::TypeRef p = widen::strip(param);
    widen::Type::Form f = widen::form(p);
    if (f == widen::Type::Form::kPointer || f == widen::Type::Form::kIterator)
        p = widen::strip(widen::get(p).pointee);
    return widen::form(p) == widen::Type::Form::kSlid ? p : widen::kNoType;
}
bool stampClassUnary(parse::Tree& tree, parse::Node& e, std::string const& op,
                     widen::TypeRef oref, diagnostic::Sink& diag);
void checkValueAssign(parse::Tree& tree, widen::TypeRef dest, parse::Node& rhs,
                      diagnostic::Sink& diag);

// A class is ABSTRACT when the most-derived declaration of some virtual method (over the
// class + its base chain) is PURE (`= delete`) — that vtable slot has no implementation.
// An abstract class cannot be instantiated as a complete object (only as a base subobject
// of a concrete derived class). classAndBaseFrames is most-derived first, so the first
// declaration seen for each signature is the winning one.
bool classIsAbstract(parse::Tree const& tree, widen::TypeRef cls) {
    std::set<std::string> seen;
    for (int fr : parse::classAndBaseFrames(tree, widen::strip(cls))) {
        if (fr < 0) continue;
        for (parse::Entry const& e : tree.entries) {
            if (e.kind != parse::EntryKind::kFunction || e.owner_ns_frame != fr
                || !e.is_virtual)
                continue;
            std::string key = e.name;
            for (std::size_t i = 1; i < e.param_types.size(); i++)
                key += "|" + std::to_string(widen::deepStrip(e.param_types[i]));
            if (!seen.insert(key).second) continue;   // a more-derived decl already won
            if (e.is_pure) return true;               // an un-overridden pure slot
        }
    }
    return false;
}

// Report + return true if `cls` is abstract and being instantiated as a complete object.
bool rejectAbstractInstantiation(parse::Tree const& tree, widen::TypeRef cls,
                                 int file_id, int tok, diagnostic::Sink& diag) {
    if (!classIsAbstract(tree, cls)) return false;
    diagnostic::report(diag, {file_id, tok,
        "Cannot instantiate the abstract class '" + widen::spellOrEmpty(widen::strip(cls))
        + "'; it has an unimplemented (pure) virtual method.", {}});
    return true;
}

// A user operator method's name is "op" + an operator symbol; a normal identifier
// method ("optimize") has an alphanumeric/underscore character after "op".
bool isOperatorName(std::string const& name) {
    if (name.size() <= 2 || name[0] != 'o' || name[1] != 'p') return false;
    char c = name[2];
    bool ident = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '_';
    return !ident;
}

// Type-dependent operator-signature rules (stage 1b), checked once resolve has typed
// the signature — the parser already enforced the syntactic ones (arity, no default
// parameter values, no misplaced `mutable`). `fn` is a class method: params[0] is the
// `_$recv` receiver, params[1..] the user parameters. Rules (canon lines 126-134 +
// the per-catalog return types): comparison and arity-0 unary return a built-in;
// index and deref return a reference; every other operator produces self (returns
// void); a move POINTER parameter and the swap parameter must be `mutable`, and the
// swap parameter is a reference to this same class. (The "a parameter must be a
// primitive or a reference, never a class by value" rule is already enforced for
// EVERY function parameter by classifyFunctionSignature, so it isn't repeated here.)
void validateOperatorSignatureTypes(parse::Node& fn, diagnostic::Sink& diag) {
    std::string const& name = fn.name;
    std::string sym = name.substr(2);
    std::vector<parse::Node*> uparams;
    for (std::size_t i = 1; i < fn.params.size(); i++)
        if (fn.params[i]) uparams.push_back(fn.params[i].get());
    std::size_t n = uparams.size();
    widen::TypeRef ret = fn.return_type;
    widen::TypeRef vd = widen::intern("void");

    bool isCmp = (sym == "==" || sym == "!=" || sym == "<" || sym == ">"
               || sym == "<=" || sym == ">=");
    bool isArity0Unary = (sym == "+" || sym == "-" || sym == "~" || sym == "!") && n == 0;
    bool isDeref = (sym == "^" && n == 0);
    bool isIndex = (sym == "[]");

    // Return type by category.
    if (isCmp || isArity0Unary) {
        if (!(isNumericType(ret) || isPtrLikeType(ret))) {
            diagnostic::report(diag, {fn.file_id, fn.name_tok,
                "The '" + name + "' operator must return a built-in type "
                "(bool, an integer, a float, or a pointer).", {}});
        }
    } else if (isIndex || isDeref) {
        if (!isReference(ret)) {
            diagnostic::report(diag, {fn.file_id, fn.name_tok,
                "The '" + name + "' operator must return a reference '^'.", {}});
        }
    } else if (ret != vd) {
        diagnostic::report(diag, {fn.file_id, fn.name_tok,
            "The '" + name + "' operator produces self and must not have a "
            "return type.", {}});
    }

    // Move / swap mutability (the type-dependent half of the `mutable` rule).
    widen::TypeRef selfCls = fn.params.empty() ? widen::kNoType
                           : pointeeType(fn.params[0]->return_type);
    if (sym == "<--" && n == 1) {
        parse::Node* p = uparams[0];
        if (isReference(p->return_type) && !p->is_mutable) {
            diagnostic::report(diag, {p->file_id, p->name_tok,
                "A move operator's pointer parameter must be 'mutable'.", {}});
        }
    } else if (sym == "<-->" && n == 1) {
        parse::Node* p = uparams[0];
        bool same = isReference(p->return_type) && selfCls != widen::kNoType
            && widen::deepStrip(pointeeType(p->return_type)) == widen::deepStrip(selfCls);
        if (!same) {
            diagnostic::report(diag, {p->file_id, p->name_tok,
                "A swap operator's parameter must be a reference to the same "
                "class.", {}});
        } else if (!p->is_mutable) {
            diagnostic::report(diag, {p->file_id, p->name_tok,
                "A swap operator's parameter must be 'mutable'.", {}});
        }
    }

    // A BINARY operator is the only 2-parameter shape in the catalog (canon 160-173:
    // `op+(Class^ a, ConstType b)`), and its FIRST parameter is the LEFT operand — which
    // IS the enclosing class: the operator produces self from (self-class lhs, rhs). So
    // param0 must be a reference to this same class, exactly like the swap rule above.
    // This pins binary dispatch to the LHS OPERAND's class, with no expected-type/context
    // steering — which is what operator PRECEDENCE demands: `b + c` must have a meaning
    // before the `=` in `a = b + c` is ever consulted. An arbitrary `A:op+(B, C)` (a class
    // whose binary op takes two unrelated types) is rejected here.
    if (n == 2) {
        parse::Node* p = uparams[0];
        bool same = isReference(p->return_type) && selfCls != widen::kNoType
            && widen::deepStrip(pointeeType(p->return_type)) == widen::deepStrip(selfCls);
        if (!same) {
            diagnostic::report(diag, {p->file_id, p->name_tok,
                "A binary operator's first parameter must be a reference to the "
                "enclosing class.", {}});
        }
    }
}

// Type-check a SCOPE's member bodies — one uniform recursion over any declaration
// scope (program, namespace, or class). A member function (method, ctor, dtor, or
// free function) gets its body typed; a const member's init is inferred + checked;
// a nested scope (namespace OR class, to any depth) recurses through the SAME
// routine — so class-in-namespace and namespace-in-class are typed identically,
// with no per-context arm. Member bodies MUST be typed here, else desugar lowers an
// un-typed field access.
void classifyScope(parse::Tree& tree, parse::Node& node, diagnostic::Sink& diag) {
    // A by-value field of an ABSTRACT class is ill-formed — it would be an incomplete
    // abstract subobject (its pure vtable slots are null). The base subobject (`_$base`)
    // is exempt: a concrete derived overrides the pure slots. A pointer/reference field is
    // fine — that IS polymorphism. Checked once here, at the field.
    if (node.kind == parse::Kind::kClassDef) {
        for (auto& p : node.params) {
            if (!p || p->name == "_$base" || p->name == "_$vptr") continue;
            widen::TypeRef ft = widen::deepStrip(p->return_type);
            if (widen::form(ft) == widen::Type::Form::kSlid
                && classIsAbstract(tree, ft)) {
                diagnostic::report(diag, {p->file_id, p->name_tok,
                    "Field '" + p->name + "' cannot embed the abstract class '"
                    + widen::spellOrEmpty(ft) + "' by value; use a reference '^'.", {}});
            }
        }
    }
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kFunctionDef) {
            // A TEMPLATE's body stays pristine — instances are typed at
            // instantiation (classifyTemplateCall) and spliced in after the walk.
            if (!m->type_params.empty()) continue;
            if (isOperatorName(m->name)) {
                // No naked operators: an operator is a class method. Inside a class,
                // validate its type-dependent signature; anywhere else, reject it.
                if (node.kind == parse::Kind::kClassDef) {
                    validateOperatorSignatureTypes(*m, diag);
                } else {
                    diagnostic::report(diag, {m->file_id, m->name_tok,
                        "An operator can only be defined as a method of a class.", {}});
                }
            }
            classifyFunctionBody(tree, *m, diag);
        } else if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            for (auto& init : m->children) {
                if (init) {
                    inferExpr(tree, *init, m->return_type, diag);
                    checkValueAssign(tree, m->return_type, *init, diag);
                }
            }
        } else if (m->kind == parse::Kind::kVarDeclStmt && m->is_global) {
            // A scope-level GLOBAL is constructed exactly like a local: run the
            // construction funnel so a class global gets its field-default tuple and an
            // array/tuple initializer is type-inferred (codegen's synthesized ctor
            // thunk then stores it / fires the ctors). Block-scope globals already
            // reach this via classifyStmt.
            classifyStmt(tree, *m, widen::kNoType, diag, nullptr);
        } else if (m->kind == parse::Kind::kNamespaceDecl
                || m->kind == parse::Kind::kClassDef) {
            // A CLASS TEMPLATE is a pattern in pristine parse state — instances
            // are typed at instantiation and spliced in resolved.
            if (m->kind == parse::Kind::kClassDef && !m->type_params.empty())
                continue;
            classifyScope(tree, *m, diag);
        }
    }
}

// A range slice `base[lo..hi]` used as a print segment (kIndexExpr, text ".."):
// the substring `hi - lo` chars from `base + lo`. STRICTLY __print-only — the base
// must be a char[] string and the bounds integers; the print backend emits a
// length-bounded write (not NUL-terminated). This is NOT general array slicing:
// the generic kIndexExpr path rejects a ".." node anywhere but here.
void inferPrintSlice(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    assert(e.children.size() == 3 && "print slice needs [base, lo, hi]");
    parse::Node& base = *e.children[0];
    parse::Node& lo = *e.children[1];
    parse::Node& hi = *e.children[2];
    inferExpr(tree, base, widen::kNoType, diag);
    inferExpr(tree, lo, widen::kNoType, diag);
    inferExpr(tree, hi, widen::kNoType, diag);
    // A sized char array decays to its element pointer, like a bare char[] segment.
    if (isArrayType(base.inferred_type)
        && widen::deepStrip(arrayFirstElem(base.inferred_type))
           == widen::deepStrip(widen::intern("char"))) {
        wrapArrayAsElemAddr(base);
        inferExpr(tree, base, widen::kNoType, diag);
    }
    if (widen::deepStrip(base.inferred_type) != widen::intern("char[]")) {
        diagnostic::report(diag, {e.file_id, e.tok,
            "A '[a..b]' print slice requires a char[] operand, not '"
            + widen::spell(base.inferred_type) + "'.", {}});
        return;
    }
    for (parse::Node const* b : {&lo, &hi}) {
        widen::TypeKind k;
        if (!widen::classify(b->inferred_type, k)
            || k.cat == widen::Category::kFloat) {
            diagnostic::report(diag, {e.file_id, e.tok,
                "A '[a..b]' print slice bound must be an integer.", {}});
            return;
        }
    }
    e.inferred_type = widen::intern("char[]");   // the print backend keys on the node
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
    if (e.kind == parse::Kind::kIndexExpr && e.text == "..") {
        inferPrintSlice(tree, e, diag);
        return;
    }
    inferExpr(tree, e, widen::kNoType, diag);
    // A sized char array (`char[N]`) decays to its element pointer `char[]` so the
    // print backend's char[] (%s) branch prints it — the same array->pointer decay
    // that fires at assignment sites, applied at the print SITE. Only a char array
    // decays: a non-char array has no string meaning and stays unsupported.
    if (isArrayType(e.inferred_type)
        && widen::deepStrip(arrayFirstElem(e.inferred_type))
           == widen::deepStrip(widen::intern("char"))) {
        wrapArrayAsElemAddr(e);
        inferExpr(tree, e, widen::kNoType, diag);
    }
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
    widen::TypeRef pointee = widen::get(widen::strip(param)).pointee;
    // An array whose pointee is its ELEMENT type (`int^` for an `int[5]` arg) is a
    // DECAY to `^arr[0]`, NOT a by-ref pass — return kNoType so the pointer path
    // decays it (else the arg is wrongly checked against the element, `int[5]` vs
    // `int`). A WHOLE-array pointee (`int[5]^`, exact OR per-leaf widen) still
    // auto-refs as before.
    if (af == F::kArray
        && widen::deepStrip(pointee)
           == widen::deepStrip(arrayFirstElem(arg.inferred_type)))
        return widen::kNoType;
    return pointee;
}

// The rung for a valid within-family WIDENING of scalar `at` to param `pt`, keyed
// on the TARGET width and the sign relation: same-sign 3/4/5, cross-sign 6/7/8 for
// a 16/32/64-bit target. Caller has already confirmed `at` widens to `pt`.
int widenRung(widen::TypeRef at, widen::TypeRef pt) {
    widen::TypeKind ka, kp;
    widen::classify(at, ka);
    widen::classify(pt, kp);
    bool same_sign = (ka.cat == kp.cat);
    int w = (kp.bits <= 16) ? 0 : (kp.bits <= 32) ? 1 : 2;
    return (same_sign ? 3 : 6) + w;
}

// The max widen rung over the leaves of two shape-matched types (or the scalar rung
// at a leaf) — grades a value->reference pass whose pointee widens per leaf. Shapes
// are known to match (shapesAndLeavesMatch); at least one leaf widens here.
int leafWidenRung(widen::TypeRef pt, widen::TypeRef at) {
    pt = widen::strip(pt);
    at = widen::strip(at);
    if (isAggregateType(pt) && isAggregateType(at)) {
        int n = aggregateSlotCount(pt), best = 0;
        for (int i = 0; i < n; i++)
            best = std::max(best, leafWidenRung(aggregateSlotType(pt, i),
                                                aggregateSlotType(at, i)));
        return best;
    }
    if (sameClass(at, pt)) return 0;
    return widenRung(at, pt);
}

// Conversion RUNG of argument `a` to parameter type `praw`, for overload ranking. A
// candidate's score is the MAX rung over its args (rankOverload); lowest score wins,
// a tie is "Ambiguous call", none viable is "No matching overload". Ladder (tighter
// -> looser); -1 = not viable (a narrowing / cross-family arg rejects the candidate):
//   0 exact   — same class; the whole-array `^arr` + value->ref convenience (no part
//               in matching); mut->const (recursive); a pointer exact modulo alias/const
//   1 alias   — same underlying type crossed through a user `alias` name
//   2 cast    — a SINGLE implicit pointer cast: nullptr->any, ->void^, ->intptr,
//               iterator->reference, array element decay, or a derived->base demotion
//   3/4/5     — smallest same-sign widening to a 16/32/64-bit target
//   6/7/8     — smallest cross-sign widening (value-preserving unsigned->wider signed)
int argConvertCost(parse::Tree& tree, parse::Node const& a, widen::TypeRef praw) {
    widen::TypeRef araw = a.inferred_type;
    widen::TypeRef param = widen::strip(praw);
    widen::TypeRef at = widen::strip(araw);
    // An ARRAY argument decays to a pointer. The WHOLE-array ref (`^arr`) is the
    // convenience pass and plays no part in matching -> EXACT (0); the ELEMENT decay
    // (`^arr[0]`) is one implicit cast (2). So `arr` matching both an `int[5]^` and an
    // `int[]` param picks the whole-ref exactly — no longer ambiguous.
    if (param != widen::kNoType && isArrayType(at) && isPtrLikeType(param)) {
        widen::TypeRef pointee = pointeeType(param);
        if (pointee != widen::kNoType) {
            if (isArrayType(pointee)
                    && shapesAndLeavesMatch(widen::strip(pointee), at))
                return 0;
            if (widen::deepStrip(pointee) == widen::deepStrip(arrayFirstElem(at)))
                return 2;
        }
        return -1;
    }
    // A non-primitive VALUE auto-promotes to a reference param (convenience, no part
    // in matching): exact when the pointee matches, else a per-leaf widen.
    if (param != widen::kNoType) {
        widen::TypeRef pointee = autoRefPointee(param, a);
        if (pointee != widen::kNoType) {
            widen::TypeRef ps = widen::strip(pointee);
            if (sameClass(at, ps)) return 0;
            // A DERIVED class VALUE binds to a BASE reference param — the SAME single
            // implicit cast (rung 2) the pointer arm below already grants an EXPLICIT
            // `^derived` -> `Base^` (ptrBaseUpcastOk). The base is the derived's slot-0
            // sub-object, so the address is unchanged; only the static type moves.
            // Without this, an argument could reach an INHERITED method / operator only by
            // spelling `^d` — a BARE `d` died here at -1. That made every user-written base
            // operator unreachable from a derived operand (`Derived + Derived` reported
            // "Operator '+' is not defined", and `Derived += Derived` fell through the
            // aug-assign hole above into invalid IR).
            if (isTransitiveBase(tree, ps, at)) return 2;
            if (shapesAndLeavesMatch(ps, at))
                return leafWidenRung(ps, at);
            return -1;
        }
    }
    if (at == widen::kNoType) return -1;
    // A pointer argument vs a pointer / `intptr` param. Exact modulo alias + const
    // (so mut->const, recursive, and a pointee alias are 0). Otherwise a SINGLE
    // implicit pointer cast (2): the strip rules (nullptr / ->void^ / ->intptr /
    // iterator->reference) OR a derived->base demotion. No chained casts.
    widen::TypeRef intptr = widen::intern("intptr");
    if (isPtrLikeType(at) && (isPtrLikeType(param) || param == intptr)) {
        if (widen::deepStrip(at) == widen::deepStrip(param)) return 0;
        if (ptrImplicitOk(at, param)) return 2;
        if (ptrBaseUpcastOk(tree, at, param)) return 2;
        return -1;
    }
    // A scalar / class VALUE. Same class is exact (0), or alias (1) when a user
    // `alias` name is crossed (same underlying type, different spelling).
    bool alias_crossed = araw != praw
        && (widen::form(araw) == widen::Type::Form::kAlias
         || widen::form(praw) == widen::Type::Form::kAlias);
    if (sameClass(at, param)) return alias_crossed ? 1 : 0;
    // A weak literal flexes to any param it FITS, graded (sign + width) against its
    // default type — so a signed literal prefers a same-sign target (int32 -> int64)
    // over a cross-sign one it merely fits (int32 -> uint64), same as a typed value.
    if (literalFlexes(a)) {
        if (!literalFitsContext(a, param)) return -1;
        return widenRung(defaultLiteralType(a), param);
    }
    // A typed value: a within-family widening graded by sign + target width, else
    // not viable (a narrowing / cross-family arg rejects the candidate).
    widen::TypeRef out;
    if (widen::commonType(at, param, out) && sameClass(out, param))
        return widenRung(at, param);
    return -1;
}

// Resolve a (possibly overloaded) user-function call: infer args, pick the best
// candidate, stamp the chosen signature. Defined after inferExpr (mutually
// recursive — it infers the argument expressions).
void classifyCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);
void classifyConstruction(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);
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
// `value_init` records that we arrived through the VALUE-INIT routing (`Class c = value`,
// buildClassFromValue's field-list fallthrough) rather than a CONSTRUCTION (`Class c(args)`).
// The two spellings hand a class field the identical slot value, so this bit is the only
// thing that tells them apart when deciding whether a class field's op= must dispatch.
std::unique_ptr<parse::Node> constructClass(parse::Tree& tree,
                                            parse::ClassInfo const& info,
                                            std::unique_ptr<parse::Node> init,
                                            int file_id, int tok,
                                            diagnostic::Sink& diag,
                                            bool subobject = false,
                                            bool value_init = false);
// Rank a class's op<sym> overloads against a source value; -1 none, -2 ambiguous
// (defined later, near the operator machinery).
int findClassOperator(parse::Tree& tree, widen::TypeRef cls, parse::Node const& rhs,
                      std::string const& opname, diagnostic::Sink& diag);
// THE class-from-VALUE funnel (defined after lowerClassConversion): build a class slot
// from a value, choosing op= CONVERSION (when a user op= accepts the value) over
// field-list construction — the expression-position twin of dispatchAssignInit. Every
// value-position class construction (tuple slot, array element, class field, `= value`
// decl) routes here so the choice is made in ONE place.
std::unique_ptr<parse::Node> buildClassFromValue(parse::Tree& tree,
                                                 parse::ClassInfo const& info,
                                                 std::unique_ptr<parse::Node> init,
                                                 int file_id, int tok,
                                                 diagnostic::Sink& diag,
                                                 bool subobject = false);

// THE argument/parameter check — the one place an ARG is validated against its PARAM
// (classifyCall's single-candidate + overload arms, and inferMethodCall). A non-primitive
// VALUE bound to a reference param rides the AUTO-REF convenience: it is passed BY ADDRESS,
// so the relation is the POINTER one, not the value one. In particular a DERIVED value binds
// to a BASE reference param — the base IS the derived's slot-0 sub-object, so the address is
// unchanged and NOTHING is sliced. Checking it as a VALUE assignment calls that a
// Derived->Base conversion and rejects it, which is what made every inherited method /
// operator unreachable from a bare (un-`^`d) argument. A genuine VALUE assignment of a
// derived to a base (`Base b = d;`) WOULD slice and is still rejected by checkSlidAssign —
// that path does not come through here.
void checkArgAssign(parse::Tree& tree, widen::TypeRef param, parse::Node& arg,
                    diagnostic::Sink& diag) {
    widen::TypeRef byref = autoRefPointee(param, arg);
    if (byref == widen::kNoType) {
        checkValueAssign(tree, param, arg, diag);
        return;
    }
    if (isTransitiveBase(tree, widen::strip(byref), widen::strip(arg.inferred_type)))
        return;   // a derived value into a base ref: an upcast, address-identical
    checkValueAssign(tree, byref, arg, diag);
}

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
            // A string literal IS STORAGE, not a pointer: N bytes in the constant pool,
            // where N is the decoded length PLUS the terminating NUL. Typing it
            // `const char[N]` makes the type say what the thing already was — sizeof has
            // always answered N here, not 8 — and lets `char a[6] = "hello"` be an
            // ordinary same-type array init instead of a special rule. It reaches every
            // `char[]` parameter through the array->element decay slids already has
            // (a non-mutable iterator param munges to `(const char)[]`, so the decayed
            // literal matches it); const is the honest half — the pool is read-only —
            // and starts being ENFORCED when const enforcement lands.
            // LIKE nullptr, IT TAKES A POINTER TYPE FROM CONTEXT. At a `char[]` / `char^`
            // target the literal decays to its element pointer, which is the address
            // codegen emits for it anyway. Doing it HERE rather than as a later rewrite
            // is what makes it stick: the decay funnel replaces a node and re-infers, and
            // a string literal that stayed a string literal would just be re-stamped as
            // the array again.
            if (isPtrLikeType(context)) {
                widen::TypeRef p = pointeeType(context);
                if (p != widen::kNoType
                    && widen::deepStrip(p) == widen::deepStrip(widen::intern("char"))) {
                    e.inferred_type = context;
                    return;
                }
            }
            e.inferred_type = widen::internConst(
                widen::internArray(widen::intern("char"),
                                   {static_cast<int>(e.text.size()) + 1}));
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
            // `^X^` CANCELS: the address of a deref IS the pointer value.
            // The class-index sugar leans on this — inferExpr just rewrote a
            // class `obj[i]` into `(obj.op[](i))^`, so `^obj[i]` is the op[]
            // call's returned reference itself. (The for-class lowering builds
            // the call directly for the same reason — resolve's shape-4/5
            // comment; the USER spelling `^self[index]` lands here.)
            if (operand.kind == parse::Kind::kDerefExpr) {
                auto inner = std::move(operand.children[0]);
                e = std::move(*inner);
                return;
            }
            if (operand.inferred_type != widen::kNoType) {
                bool indexed = (operand.kind == parse::Kind::kIndexExpr);
                e.inferred_type = indexed                         // structural — keeps an
                    ? widen::internIterator(operand.inferred_type)  // alias pointee intact
                    : widen::internPointer(operand.inferred_type);
            }
            return;
        }
        case parse::Kind::kIndexExpr: {
            // A range slice `base[lo..hi]` (text "..") is print-only; inferPrintArg
            // handles it as a segment. Reaching the general index path means it was
            // written somewhere else — reject.
            if (e.text == "..") {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "A '[a..b]' slice is only valid as a __print / __println "
                    "argument.", {}});
                return;
            }
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
            // Stage 4: a class base indexed dispatches to op[], which returns a
            // reference; the deref of that reference is the resulting lvalue —
            // `obj[i]` -> `(obj.op[](i))^` (canon 107-111). Works in read and write
            // position (a deref is an lvalue).
            if (widen::form(widen::strip(base.inferred_type)) == widen::Type::Form::kSlid
                && classHasOperatorArity(tree, base.inferred_type, "op[]", 1)) {
                auto call = std::make_unique<parse::Node>();
                call->kind = parse::Kind::kMethodCallStmt;
                call->name = "op[]";
                call->name_tok = e.tok;
                call->file_id = e.file_id;
                call->tok = e.tok;
                call->resolved_entry_id = -1;
                call->children.push_back(std::move(e.children[0]));
                call->children.push_back(std::move(e.children[1]));
                inferMethodCall(tree, *call, diag);
                widen::TypeRef refT = call->inferred_type;
                e.kind = parse::Kind::kDerefExpr;
                e.name.clear();
                e.resolved_entry_id = -1;
                e.children.clear();
                e.children.push_back(std::move(call));
                e.inferred_type = pointeeType(refT);
                return;
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
            // A tuple VALUE auto-promotes to a tuple-REFERENCE parameter, so the context
            // at such a call is `(...)^`, not `(...)`. See through it: the slots still
            // want the pointee's slot types. Invisible until a slot's type could DEPEND
            // on its context — a string literal is the first — and a slot that infers
            // context-free then needs a per-leaf conversion the aggregate convert cannot
            // express (an array element into a pointer slot).
            if (isPtrLikeType(ctx_s)) {
                widen::TypeRef pointee = pointeeType(ctx_s);
                if (pointee != widen::kNoType
                    && widen::form(widen::strip(pointee)) == widen::Type::Form::kTuple)
                    ctx_s = widen::strip(pointee);
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
                widen::TypeRef slot_s =
                    slot_ctx == widen::kNoType ? widen::kNoType
                                               : widen::strip(slot_ctx);
                // A CLASS-typed slot is CONSTRUCTED from its init value (a scalar /
                // tuple is the slot-class's ctor input), recursively — exactly like a
                // class-typed field or a class ARRAY element (classifyArrayFromTuple).
                // Rewrite the slot node in place to the construction tuple, so the
                // leaf goes through the SAME `constructClass` path as `Point pt = 0`.
                if (slot_ctx != widen::kNoType
                    && widen::form(slot_s) == widen::Type::Form::kSlid
                    && tree.classes.count(slot_s) > 0) {
                    parse::ClassInfo const& sub = tree.classes.at(slot_s);
                    int c_file = e.children[i]->file_id, c_tok = e.children[i]->tok;
                    auto init = std::make_unique<parse::Node>(std::move(*e.children[i]));
                    auto built = buildClassFromValue(tree, sub, std::move(init),
                                                     c_file, c_tok, diag);
                    *e.children[i] = std::move(*built);
                    e.children[i]->inferred_type = slot_ctx;
                }
                // An ARRAY-typed slot taking a tuple LITERAL is array-from-tuple,
                // not a nested tuple: validate it as such and type the slot as the
                // array (so construction builds an array value, not a sub-tuple).
                else if (slot_ctx != widen::kNoType
                    && widen::form(slot_s) == widen::Type::Form::kArray
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
            // Stage 4: a class operand dereferenced dispatches to op^() (arity 0),
            // which returns a reference; its deref is the resulting lvalue —
            // `obj^` -> `(obj.op^())^` (canon 116-119). This node stays a kDerefExpr;
            // its child becomes the op^ call.
            if (widen::form(widen::strip(operand.inferred_type)) == widen::Type::Form::kSlid
                && classHasOperatorArity(tree, operand.inferred_type, "op^", 0)) {
                auto opNode = std::move(e.children[0]);
                auto call = std::make_unique<parse::Node>();
                call->kind = parse::Kind::kMethodCallStmt;
                call->name = "op^";
                call->name_tok = e.tok;
                call->file_id = e.file_id;
                call->tok = e.tok;
                call->resolved_entry_id = -1;
                call->children.push_back(std::move(opNode));
                inferMethodCall(tree, *call, diag);
                widen::TypeRef refT = call->inferred_type;
                e.children.clear();
                e.children.push_back(std::move(call));
                e.inferred_type = pointeeType(refT);
                return;
            }
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
                // A bare method NAME with no `(args)` — `obj.method` — is not a
                // value; the call is missing its parameter list. (`obj.method(args)`
                // parses straight to a kMethodCallStmt and never reaches here.)
                // Search the class AND its bases so an inherited method reports the
                // missing-`()` error, not a misleading "has no field".
                for (int frame : parse::classAndBaseFrames(tree, bt)) {
                    int mid = parse::findMemberDeclared(tree, frame, e.name);
                    if (mid >= 0
                        && tree.entries[mid].kind == parse::EntryKind::kFunction) {
                        diagnostic::report(diag, {e.file_id, e.name_tok,
                            "Function call is missing parameter list '()'.", {}});
                        return;
                    }
                }
                diagnostic::report(diag, {e.file_id, e.name_tok,
                    "Class '" + it->second.name + "' has no field '" + e.name
                    + "'.", {}});
                return;
            }
            e.inferred_type = it->second.field_types[idx];
            return;
        }
        case parse::Kind::kMethodCallStmt: {
            // A method-call EXPRESSION used as a value (`x = obj.method(args)`).
            inferMethodCall(tree, e, diag);
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
            // `new C[n]` of an imported OPAQUE class: the array indexes and constructs its
            // elements at a STATIC stride this TU does not have (only the completer knows
            // the element size). A single `new C` is fine — it needs no stride, just the
            // runtime __$sizeof() for the one object. So reject only the array form, the
            // heap twin of the rejected stack `C a[n]`. (A pointer `C^` is always allowed.)
            if (is_array && widen::form(es) == widen::Type::Form::kSlid
                && widen::slidOpaque(es)
                && widen::slidLinkage(es) == widen::Type::Linkage::kDeclare) {
                diagnostic::report(diag, {e.file_id, e.name_tok,
                    "Cannot allocate an array of imported incomplete class '" + elem
                    + "'; its element stride is not known here (allocate a single '"
                    + elem + "' with 'new " + elem + "', or hold each by pointer).", {}});
            }
            // (The abstract-class check is not here: `new Class` builds its object through
            // constructClass -> classifyClassInit below, where the check lives.)
            // Constructor args (children[2]) belong to a SINGLE class object, OR — for
            // an ARRAY new — a size-matched initializer tuple that distributes across
            // the elements exactly like the stack `T arr[k](...)` form.
            parse::Node* args = (e.children.size() > 2) ? e.children[2].get() : nullptr;
            if (is_array && args) {
                // An ARRAY new WITH an initializer distributes the tuple element-by-
                // element through the SAME array<->tuple bridge the stack form uses
                // (classifyArrayFromTuple) — no new init semantics. It needs a LITERAL
                // element count so the `T[k]` array type (and the matched-size check)
                // exist at compile time; a runtime count has no fixed shape to match a
                // fixed tuple against, so it stays default-only (init rejected).
                parse::Node& size = *e.children[0];
                if (size.kind != parse::Kind::kIntLiteral
                    && size.kind != parse::Kind::kUintLiteral) {
                    diagnostic::report(diag, {args->file_id, args->tok,
                        "An array allocation with a non-literal size cannot take an "
                        "initializer.", {}});
                } else {
                    long long k = std::strtoll(size.text.c_str(), nullptr, 10);
                    widen::TypeRef arrTy = widen::internArray(
                        e.return_type, std::vector<int>{static_cast<int>(k)});
                    // Validates the shape against T[k] (a mismatch is the same
                    // "Array initializer shape does not match" the stack form gives)
                    // and rewrites each element to its construction in place.
                    classifyArrayFromTuple(tree, arrTy, *args, diag);
                    args->inferred_type = arrTy;   // codegen: whole-array construct
                }
            } else if (args && !is_class) {
                diagnostic::report(diag, {args->file_id, args->tok,
                    "Only a class takes constructor arguments; '" + elem
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
            // An ARRAY operand DECAYS to its element pointer `^arr[0]` before the
            // reinterpret: an array is storage, not a pointer, so the cast applies to
            // its address. `<Type[]> arr` / `<Type^> arr` (canon) then reinterpret the
            // element iterator (whole-array-ref you spell `^arr`, no cast).
            if (isArrayType(operand.inferred_type)) {
                wrapArrayAsElemAddr(operand);
                inferExpr(tree, operand, widen::kNoType, diag);
            }
            std::string to = widen::spellOrEmpty(e.return_type);
            // An empty operand type means inferExpr already reported an error;
            // skip the rule check (it would cascade a second, misleading
            // diagnostic) but still stamp the target type so downstream stages
            // see a typed node — the cast's type IS the target regardless.
            if (operand.inferred_type != widen::kNoType) {
                std::string why;
                std::string ot = widen::spellOrEmpty(operand.inferred_type);
                if (!ptrExplicitOk(operand.inferred_type, e.return_type, why)
                    && !ptrBaseCastOk(tree, operand.inferred_type, e.return_type)) {
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
            // Already lowered (a class conversion rewritten to construct + op= on a
            // prior inferExpr pass) — idempotent: its type is the target.
            if (e.class_conversion) { e.inferred_type = e.return_type; return; }
            assert(e.children.size() == 1 && "kConvertExpr needs 1 operand");
            parse::Node& operand = *e.children[0];
            // `Type(value)` NAMELESS PRIMITIVE TEMPORARY: bound by the DECL-INIT rules —
            // infer the operand WITH the target as context (a literal flexes / must FIT;
            // a wider source is a narrowing ERROR), NOT the truncating conversion grid.
            if (e.is_temp_init) {
                inferExpr(tree, operand, e.return_type, diag);
                checkValueAssign(tree, e.return_type, operand, diag);
                e.inferred_type = e.return_type;
                return;
            }
            inferExpr(tree, operand, widen::kNoType, diag);
            // A CLASS target is an assignment-to-a-temp: default-construct a `_$cret`
            // of the class, dispatch its op= from the source, yield the temp (lowered
            // here, lifted by desugar). An AGGREGATE target converts BY SLOT, iteratively
            // and recursively — exactly like every other aggregate: it desugars to a
            // tuple of per-slot conversions `((T0=src[0]), (T1=src[1]))`, so a class slot
            // reuses the class path, a primitive slot the value grid, a nested aggregate
            // recurses. Both suppress a cascade on a kNoType operand (already reported).
            widen::Type::Form tf = widen::form(widen::strip(e.return_type));
            if (tf == widen::Type::Form::kSlid) {
                if (operand.inferred_type != widen::kNoType)
                    lowerClassConversion(tree, e, diag);
                else e.inferred_type = e.return_type;
                return;
            }
            if (tf == widen::Type::Form::kTuple || tf == widen::Type::Form::kArray) {
                if (operand.inferred_type != widen::kNoType)
                    lowerAggregateConversion(tree, e, diag);
                else e.inferred_type = e.return_type;
                return;
            }
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
            // A bare FUNCTION name used as a value — `x = fn` — is not a value; the
            // call is missing its parameter list. (`fn(args)` parses to a kCallExpr,
            // never a bare ident.) Without this it slips to codegen and asserts (the
            // name has no alloca).
            if (tree.entries[e.resolved_entry_id].kind
                    == parse::EntryKind::kFunction) {
                diagnostic::report(diag, {e.file_id, e.name_tok,
                    "Function call is missing parameter list '()'.", {}});
                return;
            }
            // THE CONVENTION OF CONVENIENCE: a template instance's bare-`T`
            // param bound to a class/tuple is really `(const T)^` — rewrite
            // the ident to a DEREF so every consumer (field access, receiver,
            // return, argument) sees the T lvalue and the body stays generic.
            // (`^s` composes: the addr-of arm's `^X^` cancellation hands back
            // the reference itself.) The generated INNER ident is marked
            // (tmpl_value_param) so a re-entrant infer over the deref doesn't
            // rewrite it again into a double deref.
            if (tree.entries[e.resolved_entry_id].tmpl_ref_param
                && !e.tmpl_value_param) {
                auto inner = std::make_unique<parse::Node>();
                inner->kind = parse::Kind::kIdentExpr;
                inner->name = e.name;
                inner->name_tok = e.name_tok;
                inner->file_id = e.file_id;
                inner->tok = e.tok;
                inner->resolved_entry_id = e.resolved_entry_id;
                inner->tmpl_value_param = true;   // conversion already applied
                inner->inferred_type =
                    parse::entryType(tree, e.resolved_entry_id);
                e.kind = parse::Kind::kDerefExpr;
                e.name.clear();
                e.resolved_entry_id = -1;
                e.children.clear();
                e.children.push_back(std::move(inner));
                e.inferred_type =
                    pointeeType(e.children[0]->inferred_type);
                return;
            }
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
            // A `Class(args)` construction (resolve marked it): build the per-field
            // construction tuple and type it as the class.
            if (e.is_construction) {
                classifyConstruction(tree, e, diag);
                return;
            }
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
            } else if (isAggregateType(operand.inferred_type)) {
                // A tuple / array steps every leaf (numeric ±1, iterator one
                // element), recursively — accepted exactly where its leaves are.
                if (!isIncDecable(operand.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '" + e.text + "' is not defined on type '"
                        + ot + "'.", {}});
                }
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
            // A STAMPED class unary is already resolved and typed, and carries a second
            // child (the accumulator's field tuple) — inferExpr may revisit a node, so
            // leave it alone. Mirrors stampClassBinary's stamp-once guard.
            if (e.class_op_chain) return;
            assert(e.children.size() == 1 && "UnaryExpr needs 1 child");
            parse::Node& operand = *e.children[0];
            std::string const& op = e.text;
            // `!` wants a bool-context operand (kNoType); `+ - ~` propagate the outer
            // context. Infer ONCE here so the class-operator probe below can read the
            // type (the branches no longer re-infer).
            inferExpr(tree, operand, op == "!" ? widen::kNoType : context, diag);
            // A unary +/-/~/! on a CLASS dispatches its ARITY-0 operator (op+/op-/op~/
            // op!), which returns a built-in — `-a` -> `a.op-()` (canon 98-107). Mirrors
            // the comparison rewrite: turn this node into the method-call expression;
            // inferMethodCall stamps its built-in return. Probe-gated, so a non-class /
            // no-op operand keeps the built-in unary semantics below. (An ARITY-1 unary
            // that produces self — `Class r = -a` — is a separate expression-temp lower.)
            if (widen::form(widen::strip(operand.inferred_type))
                    == widen::Type::Form::kSlid
                && classHasOperatorArity(tree, operand.inferred_type, "op" + op, 0)) {
                auto opnode = std::move(e.children[0]);
                e.kind = parse::Kind::kMethodCallStmt;
                e.name = "op" + op;
                e.name_tok = e.tok;
                e.resolved_entry_id = -1;
                e.children.clear();
                e.children.push_back(std::move(opnode));
                inferMethodCall(tree, e, diag);
                return;
            }
            // ARITY-1 unary produce-self: `Class r = -a` -> `r.op-(a)` (canon 103-104).
            // It PRODUCES a class value, exactly as a class binary does, so it takes the
            // same road: classify RESOLVES and STAMPS it, desugar's chain lowering decides
            // where the accumulator lives (the destination itself when that is raw storage,
            // a statement-scoped temp when it is a live object). Dispatch is on the OPERAND's
            // class alone — the old arm let the CONTEXT (the assignment target) pick the
            // result class, the same precedence inversion that killed the target-keyed
            // binary fuse. A cross-class destination now converts through the binding funnel
            // like any other, instead of steering the operator.
            if (op == "+" || op == "-" || op == "~" || op == "!") {
                if (stampClassUnary(tree, e, op, operand.inferred_type, diag)) return;
            }
            // An AGGREGATE operand is TAKEN APART — `-(a, b)` is `(-a, -b)`. This arm never
            // had an aggregate path at all, in classify OR codegen, so it typed the result
            // as the aggregate and then emitted `sub { i32, i32 } 0, %t` — invalid IR, with
            // no class anywhere near it. `!` is excluded: a slot-wise `!` would yield a
            // TUPLE OF BOOLS, which is the same open question as a slot-wise comparison.
            if ((op == "+" || op == "-" || op == "~")
                && isAggregateType(operand.inferred_type)) {
                explodeAggregateExpr(tree, e, diag);
                return;
            }
            if (op == "!") {
                if (operand.inferred_type != widen::kNoType
                    && !isCoercibleToBool(operand.inferred_type)) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '!' is not defined on type '"
                        + widen::spellOrEmpty(operand.inferred_type) + "'.", {}});
                }
                e.inferred_type = widen::intern("bool");
            } else {  // + - ~
                e.inferred_type = operand.inferred_type;
                e.alias_label = operand.alias_label;   // a unary keeps the label
            }
            return;
        }
        case parse::Kind::kBinaryExpr: {
            // An already-STAMPED class binary is fully resolved and carries a third child
            // (the accumulator's construction value). A statement arm may re-infer its rhs
            // with a different context; there is nothing left to decide, and no context may
            // steer it anyway — the result class is the lhs OPERAND's, full stop.
            if (e.class_op_chain) return;
            assert(e.children.size() == 2 && "BinaryExpr needs 2 children");
            parse::Node& lhs = *e.children[0];
            parse::Node& rhs = *e.children[1];
            std::string const& op = e.text;

            if (op == "&&" || op == "||" || op == "^^") {
                inferExpr(tree, lhs, widen::kNoType, diag);
                inferExpr(tree, rhs, widen::kNoType, diag);
                // A class logical binary is a produce-self op like arith/bitwise/shift —
                // `Flags f = a && b` -> `f.op&&(a, b)`. Dispatch through the shared helper
                // before the built-in bool-coercion path (which rejects a class operand).
                // The compound form `f &&= a` already dispatches in the aug-assign arm.
                if (stampClassBinary(tree, e, op, lhs.inferred_type, diag)) {
                    return;
                }
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
                // A class shift is a produce-self binary op like arith/bitwise —
                // `Shift c = a << b` -> `c.op<<(a, b)`. Dispatch through the shared
                // helper before the numeric path (which rejects a class lhs). The
                // compound form `c <<= a` already dispatches in the aug-assign arm.
                if (stampClassBinary(tree, e, op, lhs.inferred_type, diag)) {
                    return;
                }
                if (isAggregateType(lhs.inferred_type)) {
                    // An array IS a homogeneous tuple — shift SLOT-WISE. A slot's count is
                    // checked by the ordinary scalar shift arm, per slot; the only rule left
                    // HERE is the shape one, inside the explode. (A SCALAR lhs with an
                    // aggregate count is not a shift at all — it falls to the arm below,
                    // which rejects the count as not integer-class.)
                    explodeAggregateExpr(tree, e, diag);
                    return;
                } else {
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

            // Stage 4: a class LHS compared (== != < > <= >=) dispatches to its
            // comparison operator, which returns a built-in — `a == b` -> `a.op==(b)`
            // (canon 87-88). Rewrite this node in place to the method-call expression;
            // inferMethodCall stamps its built-in return as this node's type.
            if ((op == "==" || op == "!=" || op == "<" || op == ">"
                 || op == "<=" || op == ">=")
                && widen::form(widen::strip(lref)) == widen::Type::Form::kSlid
                && classHasOperatorArity(tree, lref, "op" + op, 1)) {
                auto lnode = std::move(e.children[0]);
                auto rnode = std::move(e.children[1]);
                e.kind = parse::Kind::kMethodCallStmt;
                e.name = "op" + op;
                e.name_tok = e.tok;
                e.resolved_entry_id = -1;
                e.children.clear();
                e.children.push_back(std::move(lnode));
                e.children.push_back(std::move(rnode));
                inferMethodCall(tree, e, diag);
                return;
            }

            // A class-PRODUCING arith/bitwise binary is RESOLVED and STAMPED here (canon
            // 85-88); desugar lowers it. EVERY class binary reaches here — including a direct
            // assign-statement target (`a = b + c`) and an aliasing one (`a = a + b`, whose
            // accumulator correctly reads the OLD `a`). Dispatch is on the LHS OPERAND's
            // class; a non-class lhs falls to the numeric path below. (Shift `<< >>` and the
            // logical binaries stamp through the same helper in their own arms above.)
            if (isArithBitBinaryOp(op)
                && stampClassBinary(tree, e, op, lref, diag)) {
                return;
            }

            // Aggregate op aggregate (array and/or tuple): one element-wise path,
            // matching shape. A mixed array/tuple result is a TUPLE; both-array
            // stays an array. (Tuple op SCALAR broadcast stays in the block below;
            // array op scalar falls through to the scalar reject.)
            if (isAggregateType(lref) || isAggregateType(rref)) {
                bool is_cmp = (op == "==" || op == "!=" || op == "<"
                            || op == "<=" || op == ">"  || op == ">=");
                if (is_cmp) {
                    bool anytup =
                        widen::form(widen::strip(lref)) == widen::Type::Form::kTuple
                     || widen::form(widen::strip(rref)) == widen::Type::Form::kTuple;
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Operator '" + op + "' is not defined on "
                        + std::string(anytup ? "a tuple." : "an array."), {}});
                    return;
                }
                // SCALAR BROADCAST: when exactly one side is a scalar, flex a literal
                // scalar to the aggregate's leaf type (so `int8[3] + 1` keeps int8);
                // the explode then clones it into every slot.
                bool lagg = isAggregateType(lref), ragg = isAggregateType(rref);
                if (lagg != ragg) {
                    widen::TypeRef leaf = aggregateLeafType(lagg ? lref : rref);
                    inferExpr(tree, lagg ? rhs : lhs, leaf, diag);
                }
                explodeAggregateExpr(tree, e, diag);
                return;
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
                // THE OPERANDS CONVERT FIRST, like every other binary op. This arm used
                // to demand IDENTICAL pointee types (nullptr the lone exemption), so
                // `Base^ == Derived^`, `intptr == ptr`, and `T^ == T[]` were all rejected
                // even though each is a conversion the language performs everywhere else
                // — a call, an assignment, a return. A comparison was the one place that
                // asked for identity instead of compatibility.
                // The common type comes from the SAME two predicates argConvertCost uses
                // to rank a pointer ARGUMENT: ptrImplicitOk (nullptr -> any, any ->
                // `void^` / `intptr`, iterator -> reference) and ptrBaseUpcastOk
                // (derived -> base, offset 0). ONE place decides what a pointer implicitly
                // becomes, so a comparison and a call can never drift apart on the answer.
                // Direction matters and is not a choice: pointer -> intptr is implicit
                // while intptr -> pointer needs an explicit cast, so a mixed pair always
                // settles on intptr. Try lhs->rhs, then rhs->lhs; the survivor is op_type,
                // and codegen converts BOTH operands to it (emitBinary), which is why no
                // codegen work is needed here — ptr->intptr is a ptrtoint, ptr->ptr a no-op.
                widen::TypeRef cmp_type = widen::kNoType;
                if (widen::deepStrip(lref) == widen::deepStrip(rref)) {
                    cmp_type = lref;
                } else if (ptrImplicitOk(lref, rref)
                        || ptrBaseUpcastOk(tree, lref, rref)) {
                    cmp_type = rref;
                } else if (ptrImplicitOk(rref, lref)
                        || ptrBaseUpcastOk(tree, rref, lref)) {
                    cmp_type = lref;
                }
                if (cmp_type == widen::kNoType) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Pointer comparison requires the same pointee type.",
                        {}});
                    return;
                }
                e.inferred_type = widen::intern("bool");
                e.op_type = cmp_type;
                return;
            }

            // A CLASS operand reaching here dispatched to NO operator — the arith/bitwise
            // hook (stampClassBinary) and the comparison rewrite above already had their
            // chance, and the shift / logical arms return earlier (each rejecting a class
            // cleanly). No built-in binary applies to a class value, and the commonType
            // path below would SILENTLY accept two identical class types and emit struct
            // add/icmp — invalid IR. Reject cleanly instead. (The compiler must never emit
            // invalid IR.) Nothing legitimate reaches here with a class operand.
            if (widen::form(widen::strip(lref)) == widen::Type::Form::kSlid
                || widen::form(widen::strip(rref)) == widen::Type::Form::kSlid) {
                widen::TypeRef cls =
                    widen::form(widen::strip(lref)) == widen::Type::Form::kSlid
                        ? lref : rref;
                diagnostic::report(diag, {e.file_id, e.tok,
                    "Operator '" + op + "' is not defined on class '"
                    + widen::spellOrEmpty(cls) + "'.", {}});
                return;
            }

            flexBinaryOperands(lhs, rhs);
            std::string lt = spellForMessage(lhs.inferred_type);   // for the message (an
            std::string rt = spellForMessage(rhs.inferred_type);   // alias spells 'T=int')

            widen::TypeRef optyRef;
            if (!widen::commonType(lhs.inferred_type, rhs.inferred_type, optyRef)) {
                // The arithmetic convenience: + - * / % convert an integer
                // operand to the float operand's type; everything else keeps
                // the family wall.
                optyRef = arithFloatMix(lhs.inferred_type, rhs.inferred_type, op);
                if (optyRef == widen::kNoType) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "No common type for '" + lt + "' and '"
                        + rt + "'; use an explicit type conversion.",
                        {}});
                    return;
                }
                // A weak INT literal operand re-kinds as a float literal
                // outright (its decimal text is a valid float spelling), so
                // the literal-widening path never sees a cross-family ask.
                flexIntLiteralToFloat(lhs, optyRef);
                flexIntLiteralToFloat(rhs, optyRef);
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
        case parse::Kind::kGlobalScopeStmt:
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

// The single overload-ranking core. Scores a candidate set against the user args
// (already inferred WITHOUT context) and returns the winner plus EVERY candidate
// tied for lowest total cost. recv_offset = leading params that are NOT user args
// (1 for a method/operator's `_$recv` receiver, held out of ranking). PURE — emits
// no diagnostics; the callers decide how a no-match / tie is reported. `best` is -1
// when nothing is viable; `tied` holds >1 id only on a genuine ambiguity.
struct OverloadRank {
    int best = -1;
    std::vector<int> tied;
};
OverloadRank rankOverload(parse::Tree& tree, std::vector<int> const& cands,
                          std::vector<parse::Node*> const& args, int recv_offset) {
    OverloadRank r;
    int best_cost = INT_MAX;
    for (int cid : cands) {
        parse::Entry const& e = tree.entries[cid];
        std::size_t umin = e.num_required > recv_offset
            ? (std::size_t)(e.num_required - recv_offset) : 0;
        std::size_t umax = e.param_types.size() >= (std::size_t)recv_offset
            ? e.param_types.size() - recv_offset : 0;
        if (args.size() < umin || args.size() > umax) continue;   // arity range
        int cost = 0;
        bool ok = true;
        for (std::size_t i = 0; i < args.size() && ok; i++) {
            int c = args[i]
                ? argConvertCost(tree, *args[i], e.param_types[i + recv_offset])
                : -1;
            if (c < 0) ok = false; else cost = std::max(cost, c);
        }
        if (!ok) continue;
        if (cost < best_cost) { best_cost = cost; r.tied = {cid}; }
        else if (cost == best_cost) { r.tied.push_back(cid); }
    }
    r.best = r.tied.empty() ? -1 : r.tied.front();
    return r;
}

// Spell a candidate's signature `name(t0, t1, ...)` for a diagnostic note, skipping
// the `recv_offset` receiver params so an operator/method reads as the user sees it.
std::string spellCandidate(parse::Entry const& e, int recv_offset) {
    std::string sig = e.name + "(";
    bool first = true;
    for (std::size_t i = (std::size_t)recv_offset; i < e.param_types.size(); i++) {
        if (!first) sig += ", ";
        first = false;
        sig += widen::spellOrEmpty(e.param_types[i]);
    }
    return sig + ")";
}

// The single ambiguity diagnostic: the primary `msg` at the call/source site, plus
// one note per tied candidate pointing at its declaration (the conflicting overloads
// the author must disambiguate). Shared by every ranking path.
void reportAmbiguity(parse::Tree& tree, int file_id, int tok, std::string const& msg,
                     std::vector<int> const& tied, int recv_offset,
                     diagnostic::Sink& diag) {
    std::vector<diagnostic::Note> notes;
    for (int cid : tied) {
        parse::Entry const& e = tree.entries[cid];
        int nf = e.def_file_id >= 0 ? e.def_file_id : e.file_id;
        int nt = e.def_tok >= 0 ? e.def_tok : e.tok;
        notes.push_back({nf, nt,
            "candidate '" + spellCandidate(e, recv_offset) + "' declared here"});
    }
    diagnostic::report(diag, {file_id, tok, msg, notes});
}

// Rank a candidate set and pick the lowest-total-cost overload. Reports "No matching
// overload" (no viable candidate) / "Ambiguous call" (a tie, with each conflicting
// declaration cited) and returns -1 on failure. Shared by classifyCall (functions,
// offset 0) and inferMethodCall (methods, offset 1).
// ONE-LEVEL COERCION for an overload set that matched NOTHING: at each argument position
// whose candidates all want the SAME class, wrap the argument in a `(C = arg)` conversion.
// Returns true if any argument changed, so the caller can re-rank.
//
// A RETRY, DELIBERATELY — not a rung in argConvertCost. A rung would re-rank every call in
// the language and could silently move a call that resolves today onto a different overload.
// This runs only after ranking found NO viable candidate, so a program that compiles today
// cannot change meaning: the only calls it affects are the ones that were errors.
//
// Positions where candidates disagree about WHICH class are left alone. Coercion picks a
// type, and picking one on the author's behalf when two are on offer is a guess; leaving it
// uncoerced falls through to the ordinary "No matching overload", which says so plainly.
bool coerceArgsForClassParams(parse::Tree& tree, std::vector<int> const& cands,
                              std::vector<parse::Node*> const& args, int recv_offset,
                              diagnostic::Sink& diag) {
    bool any = false;
    for (std::size_t i = 0; i < args.size(); i++) {
        if (!args[i]) continue;
        std::size_t p = i + static_cast<std::size_t>(recv_offset);
        widen::TypeRef want = widen::kNoType;
        bool conflict = false;
        for (int cid : cands) {
            parse::Entry const& e = tree.entries[cid];
            if (p >= e.param_types.size()) continue;
            widen::TypeRef target = classParamTarget(e.param_types[p]);
            if (target == widen::kNoType) continue;
            if (want == widen::kNoType) want = target;
            else if (widen::deepStrip(want) != widen::deepStrip(target)) conflict = true;
        }
        if (want == widen::kNoType || conflict) continue;
        if (coerceOperandToClass(tree, want, *args[i], diag)) any = true;
    }
    return any;
}

int pickOverload(parse::Tree& tree, std::vector<int> const& cands,
                 std::vector<parse::Node*> const& args, int recv_offset,
                 std::string const& name, int file_id, int tok,
                 diagnostic::Sink& diag) {
    OverloadRank r = rankOverload(tree, cands, args, recv_offset);
    if (r.best < 0 && coerceArgsForClassParams(tree, cands, args, recv_offset, diag))
        r = rankOverload(tree, cands, args, recv_offset);
    if (r.best < 0) {
        diagnostic::report(diag, {file_id, tok,
            "No matching overload for '" + name + "'.", {}});
        return -1;
    }
    if (r.tied.size() > 1) {
        reportAmbiguity(tree, file_id, tok,
            "Ambiguous call to '" + name + "'; multiple overloads match.",
            r.tied, recv_offset, diag);
        return -1;
    }
    return r.best;
}

// ---- Function-template calls -------------------------------------------------

// Does this pattern subtree mention a template type parameter anywhere?
bool patternHasTypeParam(widen::TypeRef t) {
    using F = widen::Type::Form;
    widen::Type const& ty = widen::get(t);
    switch (ty.form) {
        case F::kAlias:
        case F::kConst:    return patternHasTypeParam(ty.underlying);
        case F::kPointer:
        case F::kIterator: return patternHasTypeParam(ty.pointee);
        case F::kArray:    return patternHasTypeParam(ty.elem);
        case F::kTuple: {
            std::vector<widen::TypeRef> slots = ty.slots;
            for (widen::TypeRef s : slots)
                if (patternHasTypeParam(s)) return true;
            return false;
        }
        case F::kSlid:     return ty.def_id == widen::kTmplParamDefId;
        case F::kNone:
        case F::kPrimitive:
        case F::kVoid:
        case F::kAnyptr:
        case F::kTmplUse:  return false;   // uses are expanded before patterns build
    }
    return false;
}

// Unification of a template PATTERN type against a concrete argument type. Its ONE
// job is finding the T bindings: a kTmplParamDefId marker leaf binds its parameter
// EXACTLY (consistently across arguments — no widening reconciles a conflict), and
// everything else defers to normal parameter matching. Concretely:
//   - A T-FREE subtree imposes NO constraint here — the instantiated call validates
//     it through the ordinary machinery (widen / decay / coercion, the whole spec).
//   - On the way TO a T position, the SHAPE conversions a normal call performs are
//     applied: array decay (into an iterator or element-pointer parameter) and
//     materialization into a reference (`(...)^` taking an rvalue). The conversion
//     itself is still validated post-binding by the normal call path.
// On a consistency failure `conflict_name` names the parameter; on a structural
// failure it stays empty.
bool unifyTypePattern(widen::TypeRef pat, widen::TypeRef arg,
                      std::vector<std::string> const& names,
                      std::vector<widen::TypeRef>& bound,
                      std::string& conflict_name) {
    using F = widen::Type::Form;
    for (;;) {
        widen::Type const& t = widen::get(pat);
        if (t.form == F::kAlias || t.form == F::kConst) pat = t.underlying;
        else break;
    }
    for (;;) {
        widen::Type const& t = widen::get(arg);
        if (t.form == F::kAlias || t.form == F::kConst) arg = t.underlying;
        else break;
    }
    // The not-template parts match by the NORMAL rules — which run after binding,
    // so here they simply do not constrain.
    if (!patternHasTypeParam(pat)) return true;
    // Copy the pattern node's fields — the recursion interns (arena pushes), and
    // while the deque never moves nodes, holding values keeps this obviously safe.
    widen::Type::Form pform = widen::form(pat);
    if (pform == F::kSlid && widen::get(pat).def_id == widen::kTmplParamDefId) {
        std::string pname = widen::get(pat).name;
        for (std::size_t i = 0; i < names.size(); i++) {
            if (names[i] != pname) continue;
            widen::TypeRef canon = widen::removeConst(widen::deepStrip(arg));
            // A by-VALUE T meeting an ARRAY argument binds the DECAYED iterator
            // type (`"za = "` — const char[6] — binds T = char[]), the same decay
            // a by-value parameter position performs on the argument. A reference
            // or iterator PATTERN (`T^` / `T[]`) reaches its element through the
            // shape aligners below instead, so this fires only for a bare T.
            if (widen::form(canon) == F::kArray) {
                widen::Type const& at = widen::get(canon);
                widen::TypeRef elem = at.dims.size() <= 1
                    ? at.elem
                    : widen::internArray(at.elem,
                          std::vector<int>(at.dims.begin() + 1, at.dims.end()));
                canon = widen::internIterator(widen::removeConst(elem));
            }
            if (bound[i] == widen::kNoType) { bound[i] = canon; return true; }
            if (widen::deepStrip(bound[i]) == canon) return true;
            conflict_name = pname;
            return false;
        }
        return false;
    }
    widen::Type::Form aform = widen::form(arg);
    if (pform == aform) {
        switch (pform) {
            case F::kPointer:
            case F::kIterator:
                return unifyTypePattern(widen::get(pat).pointee,
                                        widen::get(arg).pointee,
                                        names, bound, conflict_name);
            case F::kArray: {
                if (widen::get(pat).dims != widen::get(arg).dims) return false;
                return unifyTypePattern(widen::get(pat).elem, widen::get(arg).elem,
                                        names, bound, conflict_name);
            }
            case F::kTuple: {
                std::vector<widen::TypeRef> ps = widen::get(pat).slots;
                std::vector<widen::TypeRef> as = widen::get(arg).slots;
                if (ps.size() != as.size()) return false;
                for (std::size_t i = 0; i < ps.size(); i++) {
                    if (!unifyTypePattern(ps[i], as[i], names, bound, conflict_name))
                        return false;
                }
                return true;
            }
            case F::kNone:
            case F::kPrimitive:
            case F::kVoid:
            case F::kAnyptr:
            case F::kSlid:
            case F::kAlias:    // peeled above; unreachable
            case F::kConst:    // peeled above; unreachable
            case F::kTmplUse:  // expanded before patterns build; unreachable
                return widen::deepStrip(pat) == widen::deepStrip(arg);
        }
        return false;
    }
    // SHAPE ALIGNERS — a normal call's conversions, applied to reach T.
    // Array decay into an iterator / element-pointer parameter (first dim drops;
    // a multi-dim array decays to a pointer at its remaining-dims element).
    if ((pform == F::kIterator || pform == F::kPointer) && aform == F::kArray) {
        widen::TypeRef elem = widen::get(arg).elem;
        std::vector<int> dims = widen::get(arg).dims;
        widen::TypeRef decayed = dims.size() <= 1
            ? elem
            : widen::internArray(elem, std::vector<int>(dims.begin() + 1, dims.end()));
        return unifyTypePattern(widen::get(pat).pointee, decayed,
                                names, bound, conflict_name);
    }
    // Materialization / auto-ref into a reference parameter: `(...)^` fed the
    // pointee's shape directly (an rvalue tuple, a class value).
    if (pform == F::kPointer) {
        return unifyTypePattern(widen::get(pat).pointee, arg,
                                names, bound, conflict_name);
    }
    return false;
}

void classifyClassSignature(parse::Tree& tree, widen::TypeRef classType,
                            diagnostic::Sink& diag);
void classifyScopeSignatures(parse::Tree& tree, parse::Node& node,
                             diagnostic::Sink& diag);

// CLASS-template instances minted while a template instance's body re-entered
// resolution (`Vec<T> w` inside a function template): resolve hands them over
// fully body-resolved; run the late stages each skipped — in pipeline order,
// BEFORE the demanding function's own body is typed (a construction there reads
// the class's folded field defaults) — and park them for the final splice.
void classifyFreshClassInstances(parse::Tree& tree, diagnostic::Sink& diag) {
    std::vector<parse::Node*> fresh = resolve::takeResolvedClassInstances(tree);
    for (parse::Node* c : fresh) {
        if (!c || diagnostic::hasErrors(diag)) break;
        constfold::runOn(tree, *c, diag);
        if (diagnostic::hasErrors(diag)) break;
        classifyClassSignature(tree, c->return_type, diag);
        classifyScopeSignatures(tree, *c, diag);
        classifyScope(tree, *c, diag);
    }
}

// Depth-guard + instantiate + run the late stages over a NEW instance (a memo hit
// just returns the existing entry). Shared by the free-function and method template
// call paths. Returns the instance entry id, or -1 (error reported).
int instantiateAndClassify(parse::Tree& tree, int tid,
                           std::vector<widen::TypeRef> const& bound,
                           parse::Node& s, diagnostic::Sink& diag) {
    if (tree.tmpl_instantiation_depth >= 64) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Template instantiation depth limit exceeded (a runaway recursive "
            "instantiation?).", {}});
        return -1;
    }
    bool created = false;
    parse::Node* inst = nullptr;
    tree.tmpl_instantiation_depth++;
    int iid = resolve::instantiateTemplate(tree, tid, bound, s.file_id, s.tok,
                                           diag, created, inst);
    if (iid >= 0 && created && inst && !diagnostic::hasErrors(diag)) {
        // Any class-template instances the body's resolution minted come first —
        // the body below may construct them.
        classifyFreshClassInstances(tree, diag);
        // The instance is an ordinary resolved function/method now — run the
        // stages its late birth skipped, in pipeline order. A DECLARATION-ONLY
        // instance (from a header's bodyless pattern) has no body to type —
        // signature only; its definition lives in the template source's TU.
        constfold::runOn(tree, *inst, diag);
        if (!diagnostic::hasErrors(diag)) {
            classifyFunctionSignature(tree, *inst, diag);
            if (inst->kind == parse::Kind::kFunctionDef) {
                classifyFunctionBody(tree, *inst, diag);
                inst->capture_types.clear();
                for (int cid : inst->captures) {
                    inst->capture_types.push_back(parse::entryType(tree, cid));
                }
            }
        }
    }
    tree.tmpl_instantiation_depth--;
    if (iid < 0 && !diagnostic::hasErrors(diag)) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Cannot instantiate template '" + s.name + "'.", {}});
    }
    return iid;
}

// Bind a template call's type-list into `bound`: the EXPLICIT list (canonicalized —
// alias/const layers dropped — so `add<Integer>` and an inferred `add(1, 2)` share
// one instance), or UNIFICATION of the argument types (classified context-free — a
// literal binds the type a typeless declaration would give) against the pattern
// params, `recv_offset` skipping a method's receiver slot. The ONE binder behind
// the free-function and method template call paths (and an operator's, when those
// land — an operator is a method). `err_tok` attributes the diagnostics. False =
// reported (explicit-arity mismatches are the caller's to report — a free
// function's was already checked at resolve).
bool bindTemplateTypeList(parse::Tree& tree, parse::Node& s, int tid,
                          std::vector<parse::Node*> const& args, int recv_offset,
                          int err_tok, diagnostic::Sink& diag,
                          std::vector<widen::TypeRef>& bound) {
    auto it = tree.templates.find(tid);
    if (it == tree.templates.end()) return false;
    std::vector<std::string> const& names = it->second.def->type_params;
    bound.assign(names.size(), widen::kNoType);
    if (!s.tmpl_args.empty()) {
        if (s.tmpl_args.size() != names.size()) return false;   // caller reports
        for (std::size_t i = 0; i < names.size(); i++)
            bound[i] = widen::removeConst(widen::deepStrip(s.tmpl_args[i]));
        return true;
    }
    for (auto* a : args) if (a) inferExpr(tree, *a, widen::kNoType, diag);
    // A COPY of the patterns — never hold entry internals across resolution.
    std::vector<widen::TypeRef> pats = tree.entries[tid].param_types;
    std::string conflict;
    for (std::size_t i = 0; i < args.size(); i++) {
        if (!args[i]) continue;
        std::size_t p = i + (std::size_t)recv_offset;
        if (p >= pats.size()) break;   // arity reported by the ordinary path
        widen::TypeRef at = args[i]->inferred_type;
        if (at == widen::kNoType) return false;   // the argument itself errored
        if (!unifyTypePattern(pats[p], at, names, bound, conflict)) {
            if (!conflict.empty()) {
                diagnostic::report(diag, {s.file_id, err_tok,
                    "Conflicting bindings for template parameter '" + conflict
                    + "' in the call to '" + s.name
                    + "'; write the type list explicitly.", {}});
            } else {
                diagnostic::report(diag, {s.file_id, err_tok,
                    "Argument " + std::to_string(i + 1) + " of '" + s.name
                    + "' (type '" + widen::spellOrEmpty(at)
                    + "') does not match the template pattern '"
                    + widen::spellOrEmpty(pats[p]) + "'.", {}});
            }
            return false;
        }
    }
    for (std::size_t i = 0; i < bound.size(); i++) {
        if (bound[i] == widen::kNoType) {
            diagnostic::report(diag, {s.file_id, err_tok,
                "Cannot infer template parameter '" + names[i]
                + "' in the call to '" + s.name
                + "'; write the type list explicitly.", {}});
            return false;
        }
    }
    return true;
}

// A call whose resolved callee is a TEMPLATE: bind the type-list, instantiate on
// demand, retarget the call at the instance entry. True = retargeted (the caller
// falls through to the ordinary call path); false = an error was reported.
bool classifyTemplateCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int tid = s.resolved_entry_id;
    std::vector<parse::Node*> args;
    for (auto& ch : s.children) args.push_back(ch.get());
    std::vector<widen::TypeRef> bound;
    if (!bindTemplateTypeList(tree, s, tid, args, /*recv_offset=*/0, s.tok, diag,
                              bound))
        return false;   // explicit-arity mismatch was reported at resolve
    int iid = instantiateAndClassify(tree, tid, bound, s, diag);
    if (iid < 0) return false;
    s.resolved_entry_id = iid;
    return true;
}

void classifyCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    // A TEMPLATE callee: bind the type-list, instantiate on demand, retarget the
    // call at the instance — which then rides the ordinary single-candidate path
    // below (defaults, conversions, the coercion retry, nested-fn captures).
    if (s.resolved_entry_id >= 0
        && tree.entries[s.resolved_entry_id].kind == parse::EntryKind::kFunction
        && tree.entries[s.resolved_entry_id].is_template) {
        if (!classifyTemplateCall(tree, s, diag)) return;
    }
    // Gather same-name free-function candidates (an unqualified call). A
    // qualified call was resolved to a single namespace member upstream; a single
    // free function keeps resolve's resolution.
    std::vector<int> cands;
    bool qualified = !s.qualifier.empty() || s.global_qualified;
    // A call resolved to a NAMESPACE / class-scope member (owner_ns_frame >= 0) — a
    // qualified `Ns:fn(...)` — gathers its overload set in that member's frame, so
    // overloads (INCLUDING function aliases) resolve by argument type rather than keeping
    // resolve's single pick. (resolve may have cleared the qualifier, so key off the entry.)
    bool member_call = s.resolved_entry_id >= 0
        && tree.entries[s.resolved_entry_id].kind == parse::EntryKind::kFunction
        && tree.entries[s.resolved_entry_id].owner_ns_frame >= 0;
    if (member_call) {
        int fr = tree.entries[s.resolved_entry_id].owner_ns_frame;
        for (std::size_t id = 0; id < tree.entries.size(); id++) {
            parse::Entry const& e = tree.entries[id];
            if (e.kind == parse::EntryKind::kFunction && e.owner_ns_frame == fr
                && e.name == s.name
                && !e.is_template && e.tmpl_args.empty())   // a template owns its
                cands.push_back((int)id);                   // name; instances share it
        }
    } else if (!qualified) {
        for (std::size_t id = 0; id < tree.entries.size(); id++) {
            parse::Entry const& e = tree.entries[id];
            // Overload candidates are FILE-SCOPE free functions (parent_frame_id
            // == the global frame 0). A nested function (in a body frame) was
            // already resolved singly by resolve and must not collide with a
            // same-named nested function in another host.
            if (e.kind == parse::EntryKind::kFunction && e.owner_ns_frame < 0
                && e.parent_frame_id == 0 && e.name == s.name
                && !e.is_template && e.tmpl_args.empty()) {   // never rank a template
                cands.push_back((int)id);                     // pattern or an instance
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
            // A LONE candidate never reaches pickOverload, so its coercion retry lives here:
            // let checkArgAssign judge, and if it refuses an argument the param's class can be
            // BUILT from, coerce one level and ask again. The rejected report is rolled back
            // first so a successful coercion leaves no trace, and re-asking is what produces
            // the real diagnostic when the coercion doesn't help (or never happened).
            std::size_t before = diag.records.size();
            checkArgAssign(tree, destRef, *s.children[i], diag);
            if (diag.records.size() != before) {
                diag.records.resize(before);
                widen::TypeRef want = classParamTarget(destRef);
                if (want != widen::kNoType)
                    coerceOperandToClass(tree, want, *s.children[i], diag);
                checkArgAssign(tree, destRef, *s.children[i], diag);
            }
        }
        return;
    }
    // 2+ overloads: infer the PROVIDED args without context, then rank by cost
    // (a candidate is viable if its arity range admits this arg count and every
    // provided arg converts; omitted trailing optionals are filled from defaults).
    for (auto& ch : s.children) {
        if (ch) inferExpr(tree, *ch, widen::kNoType, diag);
    }
    std::vector<parse::Node*> args;
    for (auto& ch : s.children) args.push_back(ch.get());
    int best = pickOverload(tree, cands, args, /*recv_offset=*/0,
                            s.name, s.file_id, s.tok, diag);
    if (best < 0) return;
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
            checkArgAssign(tree, s.param_types[i], *s.children[i], diag);
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
void checkPtrAssign(parse::Tree& tree, widen::TypeRef lvalue_type,
                    parse::Node const& rhs, diagnostic::Sink& diag) {
    // An empty type on either side rides an already-reported upstream error
    // (an unresolved name, a void value, ...) — skip silently rather than emit a
    // misleading second diagnostic. Deliberate, not a missing check.
    if (rhs.inferred_type == widen::kNoType || lvalue_type == widen::kNoType) return;
    if (!isPtrLikeType(lvalue_type) && !isPtrLikeType(rhs.inferred_type)) return;  // numeric
    if (ptrImplicitOk(rhs.inferred_type, lvalue_type)) return;
    if (ptrBaseUpcastOk(tree, rhs.inferred_type, lvalue_type)) return;  // derived^ -> base^
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
    std::string s = spellForMessage(src);    // an alias spells label=target
    std::string d = spellForMessage(dest);
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

// A SINGLE-ELEMENT array (`int[1]`, `int[1][1]`) initialized from a bare SCALAR.
// Size-1 tuples collapse to their element (`(2)` -> `2`), so the lone element's
// initializer is spelled bare — there is no 1-tuple to take the array-from-tuple
// path. Fires ONLY when the array holds exactly one element (NOT a broadcast) and
// the rhs is a scalar value (a tuple literal is array-from-tuple; an array value is
// the array-value path; a non-primitive element falls to a clean per-element error).
bool isScalarIntoUnitArray(widen::TypeRef declType, parse::Node const& rhs) {
    widen::TypeRef d = widen::strip(declType);
    if (widen::form(d) != widen::Type::Form::kArray) return false;
    if (rhs.kind == parse::Kind::kTupleExpr) return false;
    long long count = 1;
    for (int dim : widen::get(d).dims) count *= dim;
    return count == 1
        && widen::form(widen::strip(rhs.inferred_type)) == widen::Type::Form::kPrimitive;
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
            auto built = buildClassFromValue(tree, sub, std::move(init),
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

// An array initialized / assigned from a tuple VALUE — a tuple-typed expression
// that is NOT a literal (`int a[4] = t4;`). The literal case is isArrayFromTuple
// (its leaf NODES are re-inferred); a value has no node-level leaves, so it copies
// through the aggregate (codegen extractvalue per slot).
bool isArrayFromTupleValue(widen::TypeRef declType, parse::Node const& rhs) {
    return widen::form(widen::strip(declType)) == widen::Type::Form::kArray
        && rhs.kind != parse::Kind::kTupleExpr
        && widen::form(widen::strip(rhs.inferred_type)) == widen::Type::Form::kTuple;
}

// A tuple initialized / assigned from an array VALUE — the mirror of
// isArrayFromTupleValue (`(int,int,int,int) t = a1;`). A tuple lhs taking an
// array-typed rhs; copies through the aggregate. Validated form-agnostically by
// checkAggregateShapeMatch (an array IS a homogeneous tuple).
bool isTupleFromArrayValue(widen::TypeRef declType, parse::Node const& rhs) {
    return widen::form(widen::strip(declType)) == widen::Type::Form::kTuple
        && widen::form(widen::strip(rhs.inferred_type)) == widen::Type::Form::kArray;
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
    // An array IS a homogeneous tuple; both decompose to N slots. The shape match
    // is FORM-AGNOSTIC — array and tuple are interchangeable at every level
    // (`(int[2],int[2])` and `(int,int)[2]` have the same leaf shape) — so the
    // only structural errors are a slot-COUNT mismatch and aggregate-vs-scalar.
    // Per-leaf TYPE compatibility (widen / narrow / cross-family) is checkValueWiden.
    widen::TypeRef ds = widen::strip(dest);
    widen::TypeRef ss = widen::strip(src);
    bool dAgg = isAggregateType(ds);
    bool sAgg = isAggregateType(ss);
    if (dAgg != sAgg) {
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Cannot assign '" + widen::spellOrEmpty(src) + "' to '"
            + widen::spellOrEmpty(dest) + "'.", {}});
        return;
    }
    if (!dAgg) {
        // Leaf: the typed-value widen check (pointer / class leaves are covered by
        // checkPtrAssign / checkSlidAssign at the top level).
        if (!isPtrLikeType(dest) && !isPtrLikeType(src)) {
            checkValueWiden(dest, src, rhs.file_id, rhs.tok, diag);
        }
        return;
    }
    int dn = aggregateSlotCount(ds);
    int sn = aggregateSlotCount(ss);
    if (dn != sn) {
        // Two arrays whose dimensions differ read most clearly as a shape
        // mismatch; any other count mismatch (tuple arity, or a cross-form
        // count) names the differing slot counts.
        bool bothArray = widen::form(ds) == widen::Type::Form::kArray
                      && widen::form(ss) == widen::Type::Form::kArray;
        std::string detail = bothArray
            ? "'; array shape differs."
            : "'; slot count differs (" + std::to_string(sn) + " vs "
                  + std::to_string(dn) + ").";
        diagnostic::report(diag, {rhs.file_id, rhs.tok,
            "Cannot assign '" + widen::spellOrEmpty(src) + "' to '"
            + widen::spellOrEmpty(dest) + detail, {}});
        return;
    }
    for (int i = 0; i < dn; i++) {
        // Capture both slot types BEFORE recursing (a nested recursion may intern,
        // dangling a held widen::get ref).
        widen::TypeRef dSlot = aggregateSlotType(ds, i);
        widen::TypeRef sSlot = aggregateSlotType(ss, i);
        checkAggregateShapeMatch(dSlot, sSlot, rhs, diag);
    }
}

bool checkArrayValueAssign(widen::TypeRef dest, parse::Node const& rhs,
                           diagnostic::Sink& diag) {
    if (widen::form(widen::strip(rhs.inferred_type)) != widen::Type::Form::kArray)
        return false;
    checkAggregateShapeMatch(dest, rhs.inferred_type, rhs, diag);
    return true;
}

// A tuple LITERAL flexes its slots into `dest` during inferExpr (kTupleExpr), but
// ONLY when the arities already match — a wrong-size literal (`(int,int,int) t(5,5)`)
// never flexed, so its slot COUNT must be checked HERE, recursively. Codegen walks the
// DEST width and extractvalues past a short source's end (invalid IR), so an unchecked
// mismatch is a miscompile, not a soft error. Count only: per-leaf flex/narrowing stays
// with inferExpr/checkValueWiden, so a literal still fits into a smaller leaf. Recurses
// only into nested TUPLE slots — a class slot was already constructed and an array slot
// validated by classifyArrayFromTuple during inferExpr, so re-checking would double-report.
void checkTupleLiteralArity(widen::TypeRef dest, parse::Node const& lit,
                            diagnostic::Sink& diag) {
    if (lit.kind != parse::Kind::kTupleExpr) return;
    widen::TypeRef ds = widen::strip(dest);
    if (!isAggregateType(ds)) return;
    int dn = aggregateSlotCount(ds);
    int rn = static_cast<int>(lit.children.size());
    if (dn != rn) {
        diagnostic::report(diag, {lit.file_id, lit.tok,
            "Cannot assign '" + widen::spellOrEmpty(lit.inferred_type) + "' to '"
            + widen::spellOrEmpty(dest) + "'; slot count differs ("
            + std::to_string(rn) + " vs " + std::to_string(dn) + ").", {}});
        return;
    }
    for (int i = 0; i < dn; i++) {
        if (!lit.children[i]) continue;
        widen::TypeRef slot = aggregateSlotType(ds, i);
        if (widen::form(widen::strip(slot)) == widen::Type::Form::kTuple)
            checkTupleLiteralArity(slot, *lit.children[i], diag);
    }
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
                       parse::ClassInfo const& info, diagnostic::Sink& diag,
                       bool subobject = false, bool value_init = false);

// Build a fully-constructed value for class `info` from an optional init value
// (a scalar / tuple / same-class value), filling the field defaults / zeros.
// Returns the construction tuple (typed as the class). Mutually recursive with
// classifyClassInit (a class-typed field constructs its sub-class the same way).
std::unique_ptr<parse::Node> constructClass(parse::Tree& tree,
                                            parse::ClassInfo const& info,
                                            std::unique_ptr<parse::Node> init,
                                            int file_id, int tok,
                                            diagnostic::Sink& diag,
                                            bool subobject, bool value_init) {
    auto holder = std::make_unique<parse::Node>();
    holder->kind = parse::Kind::kVarDeclStmt;
    holder->file_id = file_id;
    holder->tok = tok;
    if (init) holder->children.push_back(std::move(init));
    classifyClassInit(tree, *holder, info, diag, subobject, value_init);
    return std::move(holder->children[0]);
}

// The entry id of a USER (non-synthesized) self-transfer operator `opname(Self^)` on `cls`
// (searching the class + base frames), else -1. Unlike findClassOperator this scans directly:
// it does NOT rank or require `defined`, and it EXCLUDES the compiler's memberwise default —
// so it reliably answers "would a blit past this operator lose behavior?" even for a class in
// the trivial bucket (no ctor/dtor), where findClassOperator does not surface the user self-op.
int userSelfTransferOpId(parse::Tree& tree, widen::TypeRef cls,
                         std::string const& opname) {
    widen::TypeRef cs = widen::strip(cls);
    for (int fr : parse::classAndBaseFrames(tree, cs)) {
        if (fr < 0) continue;
        for (std::size_t id = 0; id < tree.entries.size(); id++) {
            parse::Entry const& e = tree.entries[id];
            if (e.kind != parse::EntryKind::kFunction || e.synthesized
                || e.owner_ns_frame != fr || e.name != opname
                || e.param_types.size() != 2)
                continue;
            widen::TypeRef p = widen::strip(e.param_types[1]);
            if (widen::form(p) != widen::Type::Form::kPointer) continue;
            if (widen::deepStrip(widen::get(p).pointee) == widen::deepStrip(cs))
                return static_cast<int>(id);
        }
    }
    return -1;
}

void classifyClassInit(parse::Tree& tree, parse::Node& s,
                       parse::ClassInfo const& info, diagnostic::Sink& diag,
                       bool subobject, bool value_init) {
    // THE construction funnel: every instantiation of a class — a local, a `new`, a
    // temporary, an array/tuple element (init or default) — reaches here, so the
    // abstract-instantiation check lives here and nowhere else. `subobject` is set only
    // when constructing a class-typed base/field member (see the field loop): a base may
    // legitimately be abstract (a concrete derived completes its pure slots), and a
    // regular field's abstractness is diagnosed at the class DEFINITION (classifyScope),
    // so skipping the check there avoids a duplicate report.
    if (!subobject) rejectAbstractInstantiation(tree, info.type, s.file_id, s.tok, diag);
    // An OPAQUE class an importer only DECLARES is default-constructed by @C__$ctor in the
    // completer — the site places nothing, so it has nowhere to put an initializer. A class
    // with no visible fields already fails the arity check below (0 fields, N given); one
    // deriving from an opaque base has visible fields and would otherwise accept the list
    // and silently drop it, since codegen skips the site fill.
    widen::TypeRef its = widen::strip(info.type);
    if (!subobject && !s.children.empty() && s.children[0]
        && widen::slidRuntimeLayout(its)
        && widen::slidLinkage(its) == widen::Type::Linkage::kDeclare) {
        diagnostic::report(diag, {s.children[0]->file_id, s.children[0]->tok,
            "Class '" + info.name + "' derives from an imported incomplete class, so it "
            "can only be default-constructed here; its fields are placed by the module "
            "that completes the base.", {}});
        return;
    }
    std::size_t n = info.field_names.size();
    std::vector<std::unique_ptr<parse::Node>> provided;
    bool provided_built = false;
    // A non-literal AGGREGATE VALUE source spreads across the fields BY SLOT (a class
    // IS a named tuple). It is always a SIDE-EFFECT-FREE indexable lvalue here: a bare
    // variable / constant, the `_$cinit` spill temp classifyStmt makes for any other
    // source (so a side-effecting source is evaluated once), or — in recursion — an
    // index into one of those. So it is spread into per-field element accesses
    // (`src[i]`), and the per-field loop below handles partial fill, recursion into
    // class-typed fields, and per-slot conversion uniformly. A tuple LITERAL (spread
    // below), a scalar (size-1), and a same-class value (a copy) are NOT this case.
    if (!s.children.empty() && s.children[0]
        && s.children[0]->kind != parse::Kind::kTupleExpr) {
        inferExpr(tree, *s.children[0], widen::kNoType, diag);
        widen::TypeRef srcT = widen::strip(s.children[0]->inferred_type);
        // A source already of THIS class's type is a whole-object copy/move, NOT a
        // field spread: leave it in place. codegen does a whole-value store (and, for
        // a `<--` move, nulls the source's pointer leaves), and the lifecycle block
        // skips the ctor since the source is already constructed. (Requires lhs/rhs
        // to be the same type — a different class spreads/constructs below.)
        if (widen::deepStrip(s.children[0]->inferred_type)
                == widen::deepStrip(info.type)) {
            return;
        }
        bool lvalue = isReReadableLvalue(*s.children[0]);
        if (isAggregateType(srcT) && lvalue) {
            int m = aggregateSlotCount(srcT);
            for (int i = 0; i < m; i++) {
                auto idx = std::make_unique<parse::Node>();
                idx->kind = parse::Kind::kIntLiteral;
                idx->text = std::to_string(i);
                idx->file_id = s.children[0]->file_id;
                idx->tok = s.children[0]->tok;
                auto ix = std::make_unique<parse::Node>();
                ix->kind = parse::Kind::kIndexExpr;
                ix->file_id = s.children[0]->file_id;
                ix->tok = s.children[0]->tok;
                ix->children.push_back(cloneExpr(*s.children[0]));
                ix->children.push_back(std::move(idx));
                provided.push_back(std::move(ix));
            }
            provided_built = true;
        }
    }
    if (!provided_built && !s.children.empty() && s.children[0]) {
        parse::Node& init = *s.children[0];
        if (init.kind == parse::Kind::kTupleExpr) {
            for (auto& ch : init.children) provided.push_back(std::move(ch));
        } else {
            provided.push_back(std::move(s.children[0]));
        }
    }
    // Construction is FLAT: a derived class's base (`_$base`, slot 0) splices its OWN
    // fields in, so it consumes flatFieldWidth(base) initializers (0 for a data-less
    // base, 1 for a single-field base, N for a wider/transitive one); every other field
    // consumes exactly one. A running index `pi` walks the flat initializer list; the
    // arity bound is the FLAT width, not the slot count.
    int flatCap = flatFieldWidth(tree, info.type);
    if ((int)provided.size() > flatCap) {
        // Caret the first EXTRA initializer (the offender), not the decl.
        parse::Node const& extra = *provided[flatCap];
        diagnostic::report(diag, {extra.file_id, extra.tok,
            "Class '" + info.name + "' has " + std::to_string(flatCap)
            + " field(s) but " + std::to_string(provided.size())
            + " initializer(s) were given.", {}});
        provided.resize(flatCap);
    }
    auto tup = std::make_unique<parse::Node>();
    tup->kind = parse::Kind::kTupleExpr;
    tup->file_id = s.file_id;
    tup->tok = s.tok;
    std::size_t pi = 0;   // running index into the FLAT initializer list
    // UNDER-FILL IS INTENTIONAL, NOT A SILENT DEFAULT: when fewer initializers are given
    // than the class has flat fields (`B b = (10)` for a wider B, or the base sub-tuple
    // running short below), each unfilled field takes its AUTHOR DEFAULT, else a
    // zero / default-construct. This is the same partial-construction the language gives
    // a NON-derived class (`P(int a, int b); P p = (1)` -> b defaults) — the base just
    // splices its fields into the same flat sequence. OVER-fill is the only arity error
    // (reported above). Do not "fix" the `pi >= provided.size()` fall-throughs.
    for (std::size_t i = 0; i < n; i++) {
        widen::TypeRef ft = info.field_types[i];
        widen::TypeRef fts = widen::strip(ft);
        // Read the author default LIVE off the stable kParam node (constfold may
        // have replaced the default expression in place since resolve).
        parse::Node* fparam = info.field_params[i];
        parse::Node* fdefault = (fparam && !fparam->children.empty())
            ? fparam->children[0].get() : nullptr;
        bool is_base = (info.field_names[i] == "_$base");

        // The hidden `_$vptr` (root virtual class, slot 0) is synthetic: never a
        // constructor argument. Fill it with a zero (null) — the ctor stamps the real
        // vtable pointer at offset 0 — and consume NO initializer from the flat list.
        if (info.field_names[i] == "_$vptr") {
            tup->children.push_back(classZeroValue(tree, ft, s.file_id, s.tok, diag));
            continue;
        }

        // An EXPLICIT EMPTY SLOT — a null IN RANGE, from `Class c(,2,3)` / `(1,,3)` — takes
        // the field's author DEFAULT (else zero / default-construct) and CONSUMES its flat
        // position, so the values AFTER it still align. This differs from an under-filled TAIL
        // (`pi` past the end), which also defaults but consumes nothing. A base (`_$base`)
        // splices its own flat run and threads any null within it down to its own fields, so
        // leave a base to the splice branch below.
        if (!is_base && pi < provided.size() && !provided[pi]) {
            pi++;   // the empty slot consumes its position
            std::unique_ptr<parse::Node> slot;
            if (widen::form(fts) == widen::Type::Form::kSlid && tree.classes.count(fts)) {
                parse::ClassInfo const& sub = tree.classes.at(fts);
                slot = constructClass(tree, sub, fdefault ? cloneExpr(*fdefault) : nullptr,
                                      s.file_id, s.tok, diag, true);
            } else {
                slot = fdefault ? cloneExpr(*fdefault)
                                : classZeroValue(tree, ft, s.file_id, s.tok, diag);
                inferExpr(tree, *slot, ft, diag);
                checkValueAssign(tree, ft, *slot, diag);
            }
            tup->children.push_back(std::move(slot));
            continue;
        }

        // A CLASS field is constructed (not raw-filled): a value already of the
        // field's class type is a copy; a scalar / tuple is the field's constructor
        // input, recursively filled with the sub-class's defaults; no value
        // default-constructs it. A base field additionally splices flat (below).
        if (widen::form(fts) == widen::Type::Form::kSlid
            && tree.classes.count(fts)) {
            parse::ClassInfo const& sub = tree.classes.at(fts);
            bool sameClass = false;
            if (pi < provided.size() && provided[pi]) {
                inferExpr(tree, *provided[pi], ft, diag);
                sameClass =
                    widen::deepStrip(provided[pi]->inferred_type) == widen::deepStrip(ft);
            }
            std::unique_ptr<parse::Node> slot;
            if (pi < provided.size() && provided[pi] && sameClass) {
                // A same-class value flowing into a class field. The ONLY form that is fine is
                // an in-place CONSTRUCTION (`Class(1,2,3)`), which field-lists into the field —
                // one ctor, no temp. Every OTHER same-class value is a TRANSFER: a bare lvalue
                // is a COPY (op=(Class^)), any other rvalue (a call, a chain) is a MOVE
                // (op<--) — and by the copy-into order a transfer must run only AFTER the field
                // is default-constructed, a seam buried inside the enclosing class's shared
                // complete constructor where a per-site transfer cannot yet be woven. Codegen
                // instead BLITS the value in (an rvalue via a throwaway temp) and runs the
                // field's ctor over the copied bytes, skipping op= / op<--. Reject that
                // silently-wrong lowering. (A trivial field class — no ctor/dtor — has nothing
                // observable to skip, so it stays legal.)
                //
                // The rejection is for an EXPLICIT per-field class value (`Holder h(c)`,
                // `= (…, val)`). It does NOT fire for the aggregate-SPREAD path
                // (`provided_built`: `Trkpair xp = mkTrkTup()`), where a whole matching
                // aggregate is distributed across the fields — a whole-object transfer, a
                // different operation, left as it was.
                bool is_ctor = provided[pi]->is_construction;
                if (!is_ctor && !provided_built) {
                    parse::Node const& v = *provided[pi];
                    bool is_move = !isBareLvalue(v);   // an rvalue source moves; an lvalue copies
                    std::string opname = is_move ? "op<--" : "op=";
                    // A USER transfer operator makes the blit observably wrong; the compiler's
                    // SYNTHESIZED default is memberwise, so a blit past it is byte-identical and
                    // stays legal. This is the signal `needs_ctor/needs_dtor` misses — an
                    // op=-only / op<--only class with no ctor/dtor (findClassOperator does not
                    // surface a trivial-bucket class's self-op, so scan directly).
                    int cop_id = userSelfTransferOpId(tree, ft, opname);
                    // A field class is TRIVIAL for transfer only when it has no ctor/dtor AND no
                    // user copy/move operator — then the blit is byte-for-byte correct with
                    // nothing to skip, so it stays legal. Reject when the blit would skip a hook
                    // OR a user op= / op<-- (an op=-only class with no hooks still has an operator
                    // the blit would bypass). A BASE subobject is included: a WHOLE same-base
                    // value is a copy — the flat scalar splice is a different branch, never here.
                    if (sub.needs_ctor || sub.needs_dtor || cop_id >= 0) {
                        std::vector<diagnostic::Note> notes;
                        if (cop_id >= 0) {
                            parse::Entry const& op = tree.entries[cop_id];
                            notes.push_back({op.file_id, op.tok,
                                "'" + sub.name + "." + opname + "(" + sub.name
                                + "^)' declared here."});
                        }
                        std::string subj = is_base
                            ? "Base '" + sub.name + "' of '" + info.name + "'"
                            : "Class field '" + info.field_names[i] + "' of '" + info.name + "'";
                        diagnostic::report(diag, {v.file_id, v.tok,
                            subj + " cannot be initialized by "
                            + (is_move ? "moving" : "copying")
                            + " another '" + sub.name + "' value here.", notes});
                    }
                }
                slot = std::move(provided[pi++]);   // same-class value -> transfer
            } else if (is_base) {
                // FLAT: the base consumes its flat width of initializers (maybe 0). A base
                // is a subobject — an abstract base is allowed (the concrete derived
                // completes its pure slots), so its construction skips the abstract check.
                int bw = flatFieldWidth(tree, ft);
                if (bw <= 0) {
                    slot = constructClass(tree, sub, nullptr, s.file_id, s.tok, diag, true,
                                          value_init);
                } else {
                    auto subtup = std::make_unique<parse::Node>();
                    subtup->kind = parse::Kind::kTupleExpr;
                    subtup->file_id = s.file_id;
                    subtup->tok = s.tok;
                    // May take FEWER than bw if the list runs short — the base then
                    // partial-fills from its own defaults (intended; see loop header).
                    for (int k = 0; k < bw && pi < provided.size(); k++)
                        subtup->children.push_back(std::move(provided[pi++]));
                    // The base subobject inherits the enclosing value-init context, so a field
                    // inside a BASE whose op= would need to dispatch is diagnosed too.
                    slot = constructClass(tree, sub, std::move(subtup),
                                          s.file_id, s.tok, diag, true, value_init);
                }
            // A regular class field is a subobject too: its abstractness (if any) is
            // reported once at the class definition (classifyScope), so skip the check.
            } else if (pi < provided.size() && provided[pi]) {
                // A class FIELD from a (non-same-class) value: field-list construct
                // (constructClass), NOT the op= class-from-VALUE funnel. A field's transfer
                // cannot be hoisted past the enclosing ctor (splitTransferInit's field note /
                // todo.txt), so a field op= is not yet available.
                //
                // In the VALUE-INIT spelling (`Super s = ((1,2,3),4)`), each field is meant to
                // be assigned its slot value — and if the field class defines a user op= that
                // accepts the value, that operator SHOULD run (default-construct the field,
                // then op=), exactly as `Class c = (1,2,3)` does at the top level. Field-list
                // construction would silently skip it. For a tuple / scalar value a match is
                // necessarily a USER op=, so reject rather than quietly field-list. (The
                // CONSTRUCTION spelling `Super s((1,2,3),4)` wants field-list — value_init is
                // false there — and a value no op= accepts field-lists as the only option;
                // both are unaffected.)
                int op_id = (value_init && !provided_built)
                    ? findClassOperator(tree, ft, *provided[pi], "op=", diag) : -1;
                if (op_id >= 0) {
                    parse::Node const& v = *provided[pi];
                    parse::Entry const& op = tree.entries[op_id];
                    diagnostic::report(diag, {v.file_id, v.tok,
                        "Class field '" + info.field_names[i] + "' of '" + info.name
                        + "' cannot dispatch '" + sub.name
                        + ".op=' for its value-initializer here.",
                        {
                        {op.file_id, op.tok,
                         "'" + sub.name + ".op=' (which accepts this value) declared here."},
                        }});
                }
                slot = constructClass(tree, sub, std::move(provided[pi++]),
                                      s.file_id, s.tok, diag, true, value_init);
            } else if (fdefault) {
                slot = constructClass(tree, sub, cloneExpr(*fdefault),
                                      s.file_id, s.tok, diag, true);
            } else {
                slot = constructClass(tree, sub, nullptr, s.file_id, s.tok, diag, true);
            }
            tup->children.push_back(std::move(slot));
            continue;
        }

        // A non-class field: provided slot / author default / zero.
        std::unique_ptr<parse::Node> slot;
        if (pi < provided.size() && provided[pi]) slot = std::move(provided[pi++]);
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

// A `Class(args)` nameless construction (resolve marked it is_construction). Wrap
// the call's argument children into a kTupleExpr — EXACTLY the shape a declarator
// `Class name(args)` feeds — then run classifyClassInit, which validates the args
// against the class fields, fills defaults/zeros, recurses into class fields, and
// leaves children[0] = the per-field construction tuple (typed as the class). The
// node is typed as the class so isSretReturn / typeNeedsHook treat it as a slid.
void classifyConstruction(parse::Tree& tree, parse::Node& s,
                          diagnostic::Sink& diag) {
    assert(s.is_construction && s.resolved_entry_id >= 0
        && "classifyConstruction: not a marked construction");
    widen::TypeRef ctype = widen::strip(tree.entries[s.resolved_entry_id].slids_type);
    auto it = tree.classes.find(ctype);
    if (it == tree.classes.end()) {
        // An unregistered class — unreachable (resolve validated the target).
        diagnostic::report(diag, {s.file_id, s.tok,
            "Unknown class type '" + widen::spellOrEmpty(ctype) + "'.", {}});
        return;
    }
    // A bare `Class` / `Class()` names NO ctor arguments — a DEFAULT construction. Record
    // it BEFORE the args are wrapped away (the tuple is always full: defaults are filled
    // in). The operator-chain lowering reads this to decide whether a collapsed head may
    // be seeded with `op=` (empty object) or must fuse with `op<OP>=` (an op= would discard
    // the very arguments the head was constructed with).
    s.ctor_no_args = true;
    for (auto& ch : s.children) if (ch) s.ctor_no_args = false;
    // Wrap the args into a kTupleExpr init slot (the declarator's `Type name(args)`
    // shape). An empty arg list -> `()` all-default.
    auto tup = std::make_unique<parse::Node>();
    tup->kind = parse::Kind::kTupleExpr;
    tup->file_id = s.file_id;
    tup->tok = s.tok;
    for (auto& ch : s.children) if (ch) tup->children.push_back(std::move(ch));
    s.children.clear();
    s.children.push_back(std::move(tup));
    classifyClassInit(tree, s, it->second, diag);   // children[0] := construction tuple
    s.return_type = it->second.type;
    s.inferred_type = it->second.type;
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
            // Post-constfold a bool literal carries INTEGER text ("0"/"1") — that is
            // what codegen emits (`store i1 0`) and what the fit-check parses. A
            // synthesized zero runs after constfold, so it must use the same form,
            // not "false".
            n->kind = parse::Kind::kBoolLiteral;
            n->text = "0";
            return n;
        }
        // signed / unsigned integer (incl. char) -> 0
        n->kind = parse::Kind::kIntLiteral;
        n->text = "0";
        return n;
    } else if (t.form == F::kArray) {
        // An array field with no initializer -> a nested zero tuple, dims-deep,
        // each leaf the element type's zero. Snapshot the shape before recursing
        // (interning a sub-array / leaf can reallocate the type store behind `t`).
        // The caller's checkValueAssign routes this through classifyArrayFromTuple
        // (shape + per-element typing), exactly like a written array initializer.
        assert(!t.dims.empty() && "classZeroValue: kArray with no dimensions "
               "(the parser rejects a zero-size array)");
        int count = t.dims.front();
        std::vector<int> rest(t.dims.begin() + 1, t.dims.end());
        widen::TypeRef inner = rest.empty() ? t.elem
                                            : widen::internArray(t.elem, rest);
        n->kind = parse::Kind::kTupleExpr;
        for (int k = 0; k < count; k++)
            n->children.push_back(classZeroValue(tree, inner, file_id, tok, diag));
        // Type the aggregate node: codegen's array field-init emits each element
        // via emitExpr, which needs the element's type. (The class-leaf branch
        // types itself; a written initializer is typed by inferExpr — this is the
        // no-initializer synthesis that otherwise leaves the node kNoType.)
        n->inferred_type = st;
        return n;
    } else if (t.form == F::kTuple) {
        // A tuple field with no initializer -> each slot's zero, recursively.
        std::vector<widen::TypeRef> slots = t.slots;   // snapshot before recursing
        n->kind = parse::Kind::kTupleExpr;
        for (widen::TypeRef slotTy : slots)
            n->children.push_back(classZeroValue(tree, slotTy, file_id, tok, diag));
        n->inferred_type = st;
        return n;
    }
    // Any other field type (void, an unregistered class) has no defined zero
    // here — report rather than silently emit an int 0.
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
    // ARRAY -> ELEMENT POINTER decay (implicit): an array lvalue takes its element
    // address `^arr[0]` when the target is a pointer whose pointee is the array's
    // element type; rewrite to the explicit address-of so the normal pointer path
    // handles it (incl. iterator->reference demotion for an `int^` target). The
    // WHOLE-array ref (pointee == the array type) is the ARGUMENT-only convenience,
    // NOT reachable from a bare array at an assignment/return — so `int[5]^ r = arr`
    // does not rewrite and falls through to checkPtrAssign, which errors.
    if (isPtrLikeType(dest) && isArrayType(rhs.inferred_type)) {
        widen::TypeRef pointee = pointeeType(dest);
        if (pointee != widen::kNoType
            && widen::deepStrip(pointee)
               == widen::deepStrip(arrayFirstElem(rhs.inferred_type))) {
            wrapArrayAsElemAddr(rhs);
            inferExpr(tree, rhs, dest, diag);
        }
    }
    if (isScalarIntoUnitArray(dest, rhs)) {
        // `int arr[1] = 2` / `int m[1][1] = 2` — the 1-tuple==scalar collapse means
        // the sole element's initializer arrives bare. Wrap it in nested 1-element
        // tuples, ONE LEVEL PER DIM (classifyArrayFromTuple wants the literal nested
        // exactly dims-deep), and reuse the array-from-tuple path (shape check +
        // per-element widen + codegen emit).
        std::size_t ndims = widen::get(widen::strip(dest)).dims.size();
        auto node = std::make_unique<parse::Node>(std::move(rhs));
        int f = node->file_id, tk = node->tok;
        for (std::size_t k = 0; k < ndims; k++) {
            auto tup = std::make_unique<parse::Node>();
            tup->kind = parse::Kind::kTupleExpr;
            tup->file_id = f;
            tup->tok = tk;
            tup->children.push_back(std::move(node));
            node = std::move(tup);
        }
        node->inferred_type = dest;
        rhs = std::move(*node);
        classifyArrayFromTuple(tree, dest, rhs, diag);
    } else if (isArrayFromTuple(dest, rhs)) {
        classifyArrayFromTuple(tree, dest, rhs, diag);
    } else if (isArrayFromTupleValue(dest, rhs) || isTupleFromArrayValue(dest, rhs)) {
        // A CROSS-FORM aggregate VALUE copy (array <-> tuple). An array is a
        // homogeneous tuple, so the same form-agnostic shape check applies: slot
        // count at every level AND per-leaf widen (the count-only check this used
        // to do let a leaf NARROW slip through to a codegen assert).
        checkAggregateShapeMatch(dest, rhs.inferred_type, rhs, diag);
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
            // flexed its slots into `dest` via inferExpr, but only when the ARITY
            // matched — so a wrong-size literal still needs its slot COUNT checked
            // (checkTupleLiteralArity), else codegen walks the dest width past a
            // short source's end and emits invalid IR. A tuple VALUE needs the full
            // SHAPE match — codegen's per-element widen::convert handles leaf compat
            // (widen / narrow / cross-family).
            if (rhs.kind == parse::Kind::kTupleExpr) {
                checkTupleLiteralArity(dest, rhs, diag);
            } else if (rhs.inferred_type != widen::kNoType) {
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
            // A size-1 `kTupleExpr` into a NUMBER / POINTER target is the construction
            // form `Type name(value)` — the 1-tuple==scalar collapse applies at the NODE
            // level too (the `=`-grouping form collapses at parse; the `(args)` form
            // builds an explicit tuple that doesn't). Unwrap it to its element so the
            // scalar/pointer checks + codegen see a scalar, not a tuple (a tuple node in
            // a scalar slot segfaults codegen). A class dest never reaches here (it
            // constructs via constructClass); a 0/2+-element tuple already failed the
            // aggregate-mismatch arm above.
            if (rhs.kind == parse::Kind::kTupleExpr && rhs.children.size() == 1
                && rhs.children[0]
                && (widen::form(widen::strip(dest)) == widen::Type::Form::kPrimitive
                    || isPtrLikeType(dest))) {
                parse::Node child = std::move(*rhs.children[0]);
                rhs = std::move(child);
            }
            // Neither side is a tuple/array: scalar / pointer / class. The terminal
            // checks — pointer implicit-cast, strong-const widen, typed-value widen,
            // class same-type.
            checkPtrAssign(tree, dest, rhs, diag);
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

// Peel a PPID bump (`++`/`--`, pre or post) to its underlying operand lvalue.
// Under PPID the bump lifts off the lvalue, so `a[i++]` accesses element `i` at
// the op site (`a[i++] <--> a[i++]` lowers to `a[i] <--> a[i]; i++; i++` — the
// SAME element). desugar lifts the bump AFTER classify, so isSameIndex must see
// through it here. A call (`a[f()]`) is NOT a bump and is left as-is.
parse::Node const& peelBump(parse::Node const& n) {
    if ((n.kind == parse::Kind::kPreIncExpr || n.kind == parse::Kind::kPostIncExpr)
        && !n.children.empty() && n.children[0])
        return peelBump(*n.children[0]);
    return n;
}

// A PROVABLY-same, SIDE-EFFECT-FREE array index: a literal with the same value, or
// the same bare variable. A bump (`a[i++]`) is peeled to its operand first (see
// peelBump). A call (`a[f()]`) is NOT matched — `f()` is genuinely a different
// element. So self-op detection on an indexed lvalue stays conservative: reject
// only when the index cannot differ.
bool isSameIndex(parse::Node const& a0, parse::Node const& b0) {
    parse::Node const& a = peelBump(a0);
    parse::Node const& b = peelBump(b0);
    if (isLiteralKind(a.kind) && a.kind == b.kind && a.text == b.text) return true;
    return a.kind == parse::Kind::kIdentExpr && b.kind == parse::Kind::kIdentExpr
        && a.resolved_entry_id >= 0 && a.resolved_entry_id == b.resolved_entry_id;
}

// True when two move/swap operands name the SAME element — STRUCTURAL lvalue
// equality. A self-swap is a no-op and a self-move would null the source it just
// copied from, so both are rejected. Recurses the lvalue chain: a bare variable
// (kIdentExpr, same entry), a deref (`p^`, operand same), a class field (`s.f`,
// same name + base same), and an index (`a[i]`, base same AND a provably-same
// index — see isSameIndex). A non-provable index (`a[f()]`, `a[i++]`) is left.
bool isSameLvalue(parse::Node const& a, parse::Node const& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == parse::Kind::kIdentExpr)
        return a.resolved_entry_id >= 0
            && a.resolved_entry_id == b.resolved_entry_id;
    if (a.kind == parse::Kind::kDerefExpr)
        return !a.children.empty() && !b.children.empty()
            && a.children[0] && b.children[0]
            && isSameLvalue(*a.children[0], *b.children[0]);
    if (a.kind == parse::Kind::kFieldExpr)
        return a.name == b.name && !a.children.empty() && !b.children.empty()
            && a.children[0] && b.children[0]
            && isSameLvalue(*a.children[0], *b.children[0]);
    if (a.kind == parse::Kind::kIndexExpr) {
        // base same AND every index child provably-same-and-pure.
        if (a.children.empty() || a.children.size() != b.children.size())
            return false;
        if (!a.children[0] || !b.children[0]
            || !isSameLvalue(*a.children[0], *b.children[0]))
            return false;
        for (std::size_t i = 1; i < a.children.size(); i++) {
            if (!a.children[i] || !b.children[i]
                || !isSameIndex(*a.children[i], *b.children[i]))
                return false;
        }
        return true;
    }
    return false;
}

// The element types a tuple or array destructures into: a tuple's slots, or N copies
// of an array's (sub-)element type. Empty if `t` is neither (not destructurable).
std::vector<widen::TypeRef> destructureSlots(widen::TypeRef t) {
    widen::TypeRef s = widen::strip(t);
    if (widen::form(s) == widen::Type::Form::kTuple) return widen::get(s).slots;
    if (widen::form(s) == widen::Type::Form::kArray) {
        widen::Type const& at = widen::get(s);
        // Capture the row count BEFORE interning the element type: internArray may
        // grow the arena's type vector, which dangles `at` (a reference into it).
        std::size_t count = at.dims.front();
        widen::TypeRef elem = (at.dims.size() <= 1) ? at.elem
            : widen::internArray(at.elem,
                  std::vector<int>(at.dims.begin() + 1, at.dims.end()));
        return std::vector<widen::TypeRef>(count, elem);
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
                  widen::TypeRef fn_return_type, diagnostic::Sink& diag,
                  std::vector<std::unique_ptr<parse::Node>>* prelude);

// A RE-READABLE LVALUE: storage we can name once per slot without re-running a side
// effect — an ident, a field select, a deref, or an index by a literal / a variable,
// bottoming out in an ident. This is `isBareLvalue` plus evaluation-count safety (see
// its comment): a destructure reads its source once PER SLOT, so `f()^` — an lvalue,
// but one that calls f each time — must not qualify.
bool isReReadableLvalue(parse::Node const& n) {
    parse::Kind k = n.kind;
    if (k == parse::Kind::kIdentExpr) return true;
    if (k == parse::Kind::kFieldExpr || k == parse::Kind::kDerefExpr) {
        return !n.children.empty() && n.children[0]
            && isReReadableLvalue(*n.children[0]);
    }
    if (k == parse::Kind::kIndexExpr) {
        return n.children.size() == 2 && n.children[0] && n.children[1]
            && isReReadableLvalue(*n.children[0])
            && (n.children[1]->kind == parse::Kind::kIntLiteral
                || n.children[1]->kind == parse::Kind::kIdentExpr);
    }
    return false;
}

// ── THE SPILL FUNNEL ───────────────────────────────────────────────────────
// A SPILL materializes a source that must be evaluated exactly ONCE but is READ MORE THAN
// ONCE — indexed per slot, spread per field — into a temp local. Every spill mints the same
// three things (an Entry, a bare kVarDeclStmt, an ident that reads the temp back), and this
// is the one place that mints them. Four sites used to hand-roll it.
//
// THE TEMP'S LIFETIME IS THE CALLER'S ONE DECISION — and it is a decision, not a side effect
// of where the decl happens to get parked. A spill is a TEMPORARY: it must die at the
// SEMICOLON. There are exactly two ways to place `decl` so that it does:
//   SEQ   — the temp is read by ONE expression (the statement's rhs). Park the decl ON THE
//           NODE (`agg_conv_spill`, children = [decl, value]) and let desugar hoist it into
//           the statement's kSeqExpr, whose teardown destroys it at the semicolon.
//   GROUP — the temp is read by SEVERAL SIBLING STATEMENTS (a destructure's per-slot stores).
//           Put the decl and those statements in a BLOCK — and hoist any DECLARING slot OUT
//           of it, because a declared name has to outlive the block.
// Pushing the decl straight into the `prelude` is the third way, and it is WRONG: a prelude
// statement is just another local in the ENCLOSING block, so the temp lives to the end of the
// SCOPE. Three sites did exactly that, each having re-derived the lifetime by accident — the
// duplication is what let them disagree.
struct Spill {
    std::unique_ptr<parse::Node> decl;   // the temp's declaration — the caller PLACES it
    std::unique_ptr<parse::Node> read;   // an ident naming the temp
};
Spill spillToTemp(parse::Tree& tree, std::unique_ptr<parse::Node> src,
                  widen::TypeRef type, char const* name) {
    int f = src->file_id, tk = src->tok;
    // A bare entry is enough: resolve is done, so the id only needs to sit below desugar's
    // minted-id range (seeded at entries.size()).
    parse::Entry e;
    e.kind = parse::EntryKind::kLocalVar;
    e.name = name;
    e.slids_type = type;
    e.file_id = f;
    e.tok = tk;
    int id = static_cast<int>(tree.entries.size());
    tree.entries.push_back(std::move(e));
    Spill sp;
    sp.decl = std::make_unique<parse::Node>();
    sp.decl->kind = parse::Kind::kVarDeclStmt;
    sp.decl->name = name;
    sp.decl->resolved_entry_id = id;
    sp.decl->return_type = type;
    sp.decl->file_id = f;
    sp.decl->tok = tk;
    sp.decl->name_tok = tk;
    sp.decl->children.push_back(std::move(src));
    sp.read = std::make_unique<parse::Node>();
    sp.read->kind = parse::Kind::kIdentExpr;
    sp.read->name = name;
    sp.read->resolved_entry_id = id;
    sp.read->inferred_type = type;
    sp.read->file_id = f;
    sp.read->tok = tk;
    return sp;
}

// The SEQ placement: wrap `value` in a node carrying [spill decl, value] that desugar's
// liftSretCallExprs hoists — the decl into the statement's `pre` (and so into its kSeqExpr),
// the node becoming `value`. So the temp is constructed with the statement's other temps and
// destroyed with them, at the semicolon.
std::unique_ptr<parse::Node> seqSpill(std::unique_ptr<parse::Node> decl,
                                      std::unique_ptr<parse::Node> value) {
    auto w = std::make_unique<parse::Node>();
    w->kind = parse::Kind::kConvertExpr;
    w->agg_conv_spill = true;
    w->inferred_type = value->inferred_type;
    w->return_type = value->return_type;
    w->file_id = value->file_id;
    w->tok = value->tok;
    w->children.push_back(std::move(decl));
    w->children.push_back(std::move(value));
    return w;
}

// Collect the entry ids a destructure's slots BIND (recursing into nested slots).
void collectDestructureTargets(parse::Node const& node, std::set<int>& ids) {
    for (std::size_t i = 1; i < node.children.size(); i++) {
        parse::Node const* slot = node.children[i].get();
        if (!slot) continue;
        if (slot->kind == parse::Kind::kDestructureStmt) {
            collectDestructureTargets(*slot, ids);
        } else if (slot->resolved_entry_id >= 0) {
            ids.insert(slot->resolved_entry_id);
        }
    }
}

// Does `n` READ any of `ids`?
bool readsAny(parse::Node const& n, std::set<int> const& ids) {
    if (n.kind == parse::Kind::kIdentExpr && n.resolved_entry_id >= 0
        && ids.count(n.resolved_entry_id)) return true;
    for (auto const& ch : n.children)
        if (ch && readsAny(*ch, ids)) return true;
    return false;
}

// Desugar EVERY destructure — COPY (`=`) / MOVE (`<--`) / SWAP (`<-->`) — into per-slot
// statements against the source, so each slot binds through the ORDINARY assignment path
// (dispatching a user op= / op<-- / op<-->, or the default when none). Applies BY SLOT,
// iteratively AND recursively:
//   (a, b)      = src   ->   a = src[0];   b = src[1];        (per-slot kAssignStmt)
//   ((a,b), c) <-- src  ->   (a,b) <-- src[0];  c <-- src[1]; (nested recurses)
//
// THE SOURCE MODEL. Every form reads the source slot by slot, so the source has to be
// something we can take apart that many times. Three shapes, in preference order:
//   LITERAL  `(x, y)`  — take element i straight OUT of the literal (COPY only). Each
//            element is then built directly into its slot: no tuple is materialized.
//   LVALUE   `t` / `t[0]` / `p^.f` — clone it and index per slot. A MOVE/SWAP nulls or
//            exchanges through the REAL storage, which is exactly why it needs one.
//   rvalue   — a COPY spills it to a temp local (evaluated once) and indexes that; a
//            MOVE/SWAP has nothing to move out of, so it is an error.
// A NESTED slot recurses with `src[i]` as its source, which is an lvalue whenever the
// outer source was one — so nesting needs no machinery of its own, works for all three
// forms, and mints no per-level temp.
// A declaring slot (`int a`) emits a no-init decl (default-construct) ahead of its
// per-slot statement.
void desugarDestructure(parse::Tree& tree, parse::Node& s,
                        widen::TypeRef fn_return_type, diagnostic::Sink& diag,
                        std::vector<std::unique_ptr<parse::Node>>& prelude) {
    bool is_move = s.default_move_init;
    bool is_swap = s.default_swap_init;
    bool is_copy = !is_move && !is_swap;
    parse::Kind stmtKind = is_swap ? parse::Kind::kSwapStmt
                         : is_move ? parse::Kind::kMoveStmt
                                   : parse::Kind::kStoreStmt;
    int f = s.file_id, tk = s.tok;

    parse::Node& rhs0 = *s.children[0];
    inferExpr(tree, rhs0, widen::kNoType, diag);
    widen::TypeRef srcType = rhs0.inferred_type;

    std::vector<widen::TypeRef> slots = destructureSlots(srcType);
    std::size_t ntargets = s.children.size() - 1;
    if (slots.empty()) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "The right side of a destructure must be a tuple or array; got '"
            + widen::spellOrEmpty(srcType) + "'.", {}});
        return;
    }
    if (ntargets != slots.size()) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Destructure has " + std::to_string(ntargets) + " target(s) but the source '"
            + widen::spellOrEmpty(srcType) + "' has "
            + std::to_string(slots.size()) + " slot(s).", {}});
        return;
    }

    // Pick the source shape (see THE SOURCE MODEL above). `srcExpr` is the expression the
    // per-slot statements read from: the literal itself (elements move out of it), the
    // lvalue (cloned per slot), or an ident naming the spill temp.
    //
    // A literal that READS A TARGET must NOT be taken apart: per-slot statements run in
    // order, so slot 0's store would be visible to slot 1's read. The whole source is
    // evaluated FIRST — that is what makes `(sa, sb) = (sb, sa)` a swap — so such a
    // literal SPILLS, like any other source that must be evaluated once up front.
    bool src_literal = is_copy
        && rhs0.kind == parse::Kind::kTupleExpr
        && rhs0.children.size() == ntargets;
    if (src_literal) {
        std::set<int> targets;
        collectDestructureTargets(s, targets);
        if (!targets.empty() && readsAny(rhs0, targets)) src_literal = false;
    }
    std::unique_ptr<parse::Node> srcExpr;
    std::unique_ptr<parse::Node> spill_decl;   // GROUP-placed below, if a spill was needed
    if (src_literal || isReReadableLvalue(rhs0)) {
        srcExpr = std::move(s.children[0]);
    } else if (!is_copy) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "A move/swap destructure requires an addressable (lvalue) source.", {}});
        return;
    } else {
        // Spill an rvalue COPY source to a temp (evaluated once), then index the temp. GROUP
        // placement (see THE SPILL FUNNEL): the temp is read by the per-slot statements, so
        // it and they go in a BLOCK and it dies at the semicolon.
        Spill sp = spillToTemp(tree, std::move(s.children[0]), srcType, "_$dsrc");
        spill_decl = std::move(sp.decl);
        srcExpr = std::move(sp.read);
    }

    // Slot i's source: the literal's element i (moved out — used exactly once), else a
    // fresh `src[i]` over a clone of the source lvalue.
    auto makeSrcElem = [&](std::size_t i) -> std::unique_ptr<parse::Node> {
        if (src_literal) return std::move(srcExpr->children[i]);
        auto lit = std::make_unique<parse::Node>();
        lit->kind = parse::Kind::kIntLiteral;
        lit->text = std::to_string(i);
        lit->file_id = f;
        lit->tok = tk;
        auto idx = std::make_unique<parse::Node>();
        idx->kind = parse::Kind::kIndexExpr;
        idx->file_id = f;
        idx->tok = tk;
        idx->children.push_back(cloneExpr(*srcExpr));
        idx->children.push_back(std::move(lit));
        return idx;
    };

    std::vector<std::unique_ptr<parse::Node>> pending;
    // Which pending statements DECLARE a slot: a declared name must outlive the group, so it
    // is the one thing that cannot go inside the block a spill needs (THE SPILL FUNNEL).
    std::vector<bool> escaping;
    bool elem_temp = false;
    for (std::size_t i = 0; i < ntargets; i++) {
        parse::Node* slot = s.children[i + 1].get();
        if (!slot) {
            // Discard slot. A literal's element still has to be EVALUATED (it may have
            // side effects, and nothing else reads it) — the other shapes evaluate the
            // whole source anyway, so there is nothing to emit for them.
            if (src_literal) {
                auto es = std::make_unique<parse::Node>();
                es->kind = parse::Kind::kExprStmt;
                es->file_id = f;
                es->tok = tk;
                es->children.push_back(makeSrcElem(i));
                pending.push_back(std::move(es));    escaping.push_back(false);
            }
            continue;
        }
        if (slot->kind == parse::Kind::kDestructureStmt) {
            // Nested: recurse with element i as the sub-source, carrying the FORM down
            // (copy / move / swap). The nested slot holds its sub-slots in children[1..];
            // children[0] is the rhs placeholder — fill it and re-classify (recurses here).
            auto sub = std::move(s.children[i + 1]);
            if (sub->children.empty()) sub->children.push_back(nullptr);
            sub->children[0] = makeSrcElem(i);
            sub->default_move_init = is_move;
            sub->default_swap_init = is_swap;
            pending.push_back(std::move(sub));   escaping.push_back(false);
            continue;
        }
        widen::TypeRef slot_ty = slots[i];
        std::string tname = slot->name;
        int tid = slot->resolved_entry_id;
        std::unique_ptr<parse::Node> elem = makeSrcElem(i);
        bool declaring = slot->kind == parse::Kind::kVarDeclStmt;
        if (declaring) {
            // Declaring slot: fix its (maybe inferred) type + register, then a decl that
            // allocates it before the per-slot statement.
            if (slot->return_type == widen::kNoType) {
                slot->return_type = slot_ty;
                if (tid >= 0) tree.entries[tid].slids_type = slot_ty;
            }
            slot_ty = slot->return_type;
            auto decl = std::make_unique<parse::Node>();
            decl->kind = parse::Kind::kVarDeclStmt;
            decl->name = tname;
            decl->name_tok = slot->name_tok;
            decl->resolved_entry_id = tid;
            decl->return_type = slot_ty;
            decl->file_id = slot->file_id;
            decl->tok = slot->tok;
            if (src_literal) {
                // A FRESH slot taking a LITERAL's element: the element is the decl's INIT,
                // so it is built DIRECTLY into the slot by the declarator funnel — one
                // construction, no tuple, no temp, no copy. This is the whole reason the
                // literal is taken apart rather than spilled.
                decl->children.push_back(std::move(elem));
                pending.push_back(std::move(decl));  escaping.push_back(true);
                continue;
            }
            // Otherwise a NO-INIT decl (default-construct) ahead of the per-slot statement.
            pending.push_back(std::move(decl));  escaping.push_back(true);
        } else {
            slot_ty = parse::entryType(tree, tid);
        }
        if (is_copy) {
            // A LIVE target cannot be assigned FROM a construction — it has no fresh slot
            // to build into (see dispatchAssignInit). Materialize such an element into its
            // own temp first, then copy that in. Only a literal source can produce one.
            if (elem->kind == parse::Kind::kCallExpr && elem->is_construction) {
                Spill sp = spillToTemp(tree, std::move(elem), slot_ty, "_$delem");
                pending.push_back(std::move(sp.decl)); escaping.push_back(false);
                elem_temp = true;
                elem = std::move(sp.read);
            }
            // COPY: a bare-name assignment `tname = src[i]`. kAssignStmt dispatches the
            // slot type's op= (or stores a primitive) — kStoreStmt is deref/index-only, so
            // a NAMED slot must go through kAssignStmt (target in name, rhs in child 0).
            auto as = std::make_unique<parse::Node>();
            as->kind = parse::Kind::kAssignStmt;
            as->name = tname;
            as->name_tok = slot->name_tok;
            as->resolved_entry_id = tid;
            as->file_id = f;
            as->tok = tk;
            as->children.push_back(std::move(elem));
            pending.push_back(std::move(as));    escaping.push_back(false);
        } else {
            // MOVE / SWAP: children [target-ident, src[i]] — the kMoveStmt / kSwapStmt shape.
            auto tgt = std::make_unique<parse::Node>();
            tgt->kind = parse::Kind::kIdentExpr;
            tgt->name = tname;
            tgt->name_tok = slot->name_tok;
            tgt->resolved_entry_id = tid;
            tgt->inferred_type = slot_ty;
            tgt->file_id = slot->file_id;
            tgt->tok = slot->tok;
            auto st = std::make_unique<parse::Node>();
            st->kind = stmtKind;
            st->file_id = f;
            st->tok = tk;
            st->children.push_back(std::move(tgt));
            st->children.push_back(std::move(elem));
            pending.push_back(std::move(st));    escaping.push_back(false);
        }
    }
    if (pending.empty()) {   // all slots discarded — nothing to bind
        s.kind = parse::Kind::kBlockStmt;
        s.children.clear();
        return;
    }
    if (spill_decl || elem_temp) {
        // GROUP placement (THE SPILL FUNNEL): the temps are read by these per-slot statements
        // and by nothing else, so they and the statements go in a BLOCK and the temps die at
        // the SEMICOLON. Only the DECLARING slots are hoisted OUT of it — a declared name has
        // to outlive the statement.
        //
        // Classifying a body statement can EMIT statements of its own, and they must be
        // sorted the same way: a NESTED destructure flattens itself into its prelude (the
        // "last stmt becomes s, the rest splice ahead" idiom), so it hands back BOTH its
        // declaring decls AND its per-slot STORES. The decls belong outside the block; the
        // stores absolutely do not — they READ the spill, so hoisting them out would run them
        // BEFORE it is assigned. (That is precisely what went wrong first: the nested `a`
        // read the spill before `mkNested()` had been evaluated into it, and came out 0.)
        // The test is who OWNS the name: a compiler temp (`_$...`) stays with the statements
        // that read it; a user-declared slot escapes.
        std::vector<std::unique_ptr<parse::Node>> body;
        auto sortEmitted = [&](std::vector<std::unique_ptr<parse::Node>>& emitted) {
            for (auto& p : emitted) {
                if (!p) continue;
                bool user_decl = p->kind == parse::Kind::kVarDeclStmt
                    && p->name.rfind("_$", 0) != 0;
                if (user_decl) prelude.push_back(std::move(p));
                else body.push_back(std::move(p));
            }
        };
        // The spill runs FIRST inside the block; everything else keeps SOURCE ORDER, so the
        // slots are declared (and therefore destroyed) in the order they were written.
        if (spill_decl) {
            std::vector<std::unique_ptr<parse::Node>> emitted;
            classifyStmt(tree, *spill_decl, fn_return_type, diag, &emitted);
            sortEmitted(emitted);
            body.push_back(std::move(spill_decl));
        }
        for (std::size_t k = 0; k < pending.size(); k++) {
            std::vector<std::unique_ptr<parse::Node>> emitted;
            classifyStmt(tree, *pending[k], fn_return_type, diag, &emitted);
            sortEmitted(emitted);
            if (escaping[k]) prelude.push_back(std::move(pending[k]));
            else            body.push_back(std::move(pending[k]));
        }
        s.kind = parse::Kind::kBlockStmt;
        s.name.clear();
        s.resolved_entry_id = -1;
        s.default_move_init = false;
        s.default_swap_init = false;
        s.children = std::move(body);   // already classified — do NOT re-classify `s`
        return;
    }
    // No temp to scope: the last pending stmt becomes `s` and the rest splice ahead, in
    // source order. Re-classifying `s` finishes it (a per-slot store/move/swap, a decl, or
    // a nested destructure).
    for (std::size_t k = 0; k + 1 < pending.size(); k++) {
        classifyStmt(tree, *pending[k], fn_return_type, diag, &prelude);
        prelude.push_back(std::move(pending[k]));
    }
    auto last = std::move(pending.back());
    s.kind = last->kind;
    s.name = last->name;
    s.resolved_entry_id = last->resolved_entry_id;
    s.return_type = last->return_type;
    s.default_move_init = last->default_move_init;
    s.default_swap_init = last->default_swap_init;
    s.children = std::move(last->children);
    classifyStmt(tree, s, fn_return_type, diag, &prelude);
}

// The slot / field types of a class-bearing target, in order (a class IS a named tuple).
std::vector<widen::TypeRef> initSlotTypes(widen::TypeRef T) {
    widen::TypeRef S = widen::strip(T);
    widen::Type::Form f = widen::form(S);
    if (f == widen::Type::Form::kTuple || f == widen::Type::Form::kSlid)
        return widen::get(S).slots;
    if (f == widen::Type::Form::kArray) return destructureSlots(S);
    return {};
}

// `path[i]` — the lvalue naming slot i of the tuple / array at `path`.
std::unique_ptr<parse::Node> makeSlotPath(parse::Node const& path, std::size_t i,
                                          widen::TypeRef slot_ty) {
    auto lit = std::make_unique<parse::Node>();
    lit->kind = parse::Kind::kIntLiteral;
    lit->text = std::to_string(i);
    lit->file_id = path.file_id;
    lit->tok = path.tok;
    auto idx = std::make_unique<parse::Node>();
    idx->kind = parse::Kind::kIndexExpr;
    idx->inferred_type = slot_ty;
    idx->file_id = path.file_id;
    idx->tok = path.tok;
    idx->children.push_back(cloneExpr(path));
    idx->children.push_back(std::move(lit));
    return idx;
}

// ---------------------------------------------------------------------------
// THE SLOT-WISE EXPLODE
// ---------------------------------------------------------------------------
//
// A TUPLE DESUGARS TO THE OPERATION BY SLOT, ITERATIVELY AND RECURSIVELY — and an ARRAY
// IS a homogeneous tuple, so this is every aggregate. `(a, b) + (c, d)` becomes the tuple
// literal `(a + c, b + d)`. Each element is then classified as an ORDINARY operation, so
// a class slot dispatches its operator, a literal flexes, a narrow widens, a chain joins
// the chain machinery — with no aggregate-specific code downstream at all. A NESTED
// aggregate slot re-enters this rewrite when its own element is classified, so the
// recursion costs nothing. The result lands through the tuple-literal distribution the
// declarator funnel already has: element i is built straight into slot i of the storage
// that owns it.
//
// WHY IT HAS TO BE HERE AND NOT IN CODEGEN — the whole reason aggregates kept breaking.
// A class operation needs an ADDRESS: a `^self` receiver and an sret destination. Codegen's
// aggregate walkers work in the VALUE domain (extractvalue / insertvalue on SSA registers),
// where a slot has no address, so they can only ever emit a NUMERIC instruction — and that
// is what they did. A class-bearing aggregate emitted `add { i32 } %a, %b`, which is not
// valid IR (widen::commonType SUCCEEDS for two identical class types, so nothing caught
// it); the unary arm never had an aggregate walker at all, so even `-(1, 2)` emitted
// `sub { i32, i32 } 0, %t`. Every aggregate bug we have had is an operation that was
// hand-written for aggregates in the value domain instead of being TAKEN APART here, where
// a slot is still an expression and can be anything the scalar path can be.
//
// EVALUATED ONCE. An operand is read by N slots, so it goes through the same trichotomy
// the destructure's source model uses (THE SPILL FUNNEL): a tuple LITERAL is taken APART
// (element i IS child i — moved, not re-evaluated); a re-readable lvalue is INDEXED per
// slot; anything else SPILLS to a temp that is indexed instead. A SCALAR operand
// BROADCASTS: cloned per slot when that is free (a literal / a re-readable lvalue),
// spilled once when it is not. SEQ placement — the temp is read by this one expression,
// so it dies at the SEMICOLON.
//
// Callers: the binary arith/bitwise arm, the shift arm, the unary arm, and the aug-assign
// statement (which explodes into per-slot STATEMENTS, so a class slot gets its `op+=`
// rather than an `op+`). Comparisons are deliberately NOT here — `(a,b) == (c,d)` is a
// semantic question (a tuple of bools, or one all-slots-equal bool?), still open.

// Safe to re-read once per slot with no temp: a literal, or a re-readable lvalue
// (isReReadableLvalue — an ident/field/deref/const-index chain, which is also what the
// destructure asks of its source).
bool isReReadableOperand(parse::Node const& e) {
    return isLiteralKind(e.kind)
        || e.kind == parse::Kind::kNullptrLiteral
        || isReReadableLvalue(e);
}

// THE take-the-source-apart spill — the ONE helper every per-slot site funnels through (the
// aggregate conversion, the class-field spread, and the slot-wise explode). A source read once
// PER SLOT must be evaluated ONCE unless it is a re-readable OPERAND (isReReadableOperand: a
// literal / nullptr / an access path with no side effect — the SAME question the destructure
// asks). When it isn't, `src` is spilled via spillToTemp: `src` becomes the temp read and the
// decl is appended to `spills` for the CALLER to PLACE — SEQ / GROUP / agg_conv_spill, the
// caller's one explicit choice (THE SPILL FUNNEL). A take-apart tuple LITERAL is the caller's
// concern (handled before the call). Returns true iff a spill was minted. This is the single
// point that answers "may I re-index this in place, or must I evaluate it once?" — the old
// per-site `isBareLvalue` gate saw only the outermost node and re-ran a side-effecting subscript
// / base (`arr[pick()]`, `get()^.f`) once per slot; todo.txt.
bool spillIfNotReReadable(parse::Tree& tree, std::unique_ptr<parse::Node>& src,
                          char const* name,
                          std::vector<std::unique_ptr<parse::Node>>& spills) {
    if (isReReadableOperand(*src)) return false;
    widen::TypeRef t = src->inferred_type;   // capture BEFORE the move
    Spill sp = spillToTemp(tree, std::move(src), t, name);
    spills.push_back(std::move(sp.decl));
    src = std::move(sp.read);
    return true;
}

// One operand of an exploding operation, prepared so slot i can be produced on demand.
struct AggOperand {
    parse::Node* node = nullptr;   // what to read (the operand, or the spill temp)
    bool aggregate = false;        // INDEX it per slot; otherwise BROADCAST (clone it)
    bool literal = false;          // a tuple LITERAL: take child i APART
};

// Prepare one operand: decide how slot i will be produced, spilling if it must be
// evaluated once. A spill decl is appended to `spills` for the caller to place (SEQ).
AggOperand prepareOperand(parse::Tree& tree, std::unique_ptr<parse::Node>& child,
                          int nslots,
                          std::vector<std::unique_ptr<parse::Node>>& spills) {
    AggOperand a;
    widen::TypeRef t = widen::strip(child->inferred_type);
    a.aggregate = isAggregateType(t);
    // A tuple literal of the right arity is taken APART — no temp, no re-evaluation.
    if (a.aggregate
        && child->kind == parse::Kind::kTupleExpr
        && static_cast<int>(child->children.size()) == nslots) {
        a.literal = true;
        a.node = child.get();
        return a;
    }
    spillIfNotReReadable(tree, child, "_$agg", spills);
    a.node = child.get();
    return a;
}

// Produce operand `a`'s value for slot i.
std::unique_ptr<parse::Node> operandSlot(AggOperand& a, std::size_t i) {
    if (a.literal) return std::move(a.node->children[i]);
    if (!a.aggregate) return cloneExpr(*a.node);          // BROADCAST
    widen::TypeRef st = aggregateSlotType(widen::strip(a.node->inferred_type),
                                          static_cast<int>(i));
    return makeSlotPath(*a.node, i, st);
}

// Explode `e` (a kBinaryExpr or kUnaryExpr with at least one AGGREGATE operand) into a
// tuple literal of per-slot operations, then classify it. Returns false when no operand
// is an aggregate (the ordinary scalar path owns the node).
bool explodeAggregateExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    // Every AGGREGATE operand must have the same slot count — a scalar one broadcasts.
    // This is the ONE shape rule now; the nested case needs no rule of its own, because a
    // nested slot pair explodes in its turn and asks the same question there.
    int n = -1;
    bool all_arrays = true;
    for (auto& c : e.children) {
        if (!c) continue;
        widen::TypeRef t = widen::strip(c->inferred_type);
        if (!isAggregateType(t)) continue;
        if (widen::form(t) != widen::Type::Form::kArray) all_arrays = false;
        int cnt = aggregateSlotCount(t);
        if (n < 0) { n = cnt; continue; }
        if (cnt != n) {
            diagnostic::report(diag, {e.file_id, e.tok,
                "Aggregate shapes differ: '"
                + widen::spellOrEmpty(e.children[0]->inferred_type) + "' vs '"
                + widen::spellOrEmpty(e.children[1]->inferred_type) + "'.", {}});
            return true;
        }
    }
    if (n <= 0) return false;

    std::vector<std::unique_ptr<parse::Node>> spills;
    std::vector<AggOperand> ops;
    for (auto& c : e.children) ops.push_back(prepareOperand(tree, c, n, spills));

    // One element per slot: the SAME operator, on the operands' slot i.
    auto tup = std::make_unique<parse::Node>();
    tup->kind = parse::Kind::kTupleExpr;
    tup->file_id = e.file_id;
    tup->tok = e.tok;
    for (std::size_t i = 0; i < static_cast<std::size_t>(n); i++) {
        auto slot = std::make_unique<parse::Node>();
        slot->kind = e.kind;                  // kBinaryExpr / kUnaryExpr
        slot->text = e.text;
        slot->file_id = e.file_id;
        slot->tok = e.tok;
        for (auto& a : ops) slot->children.push_back(operandSlot(a, i));
        tup->children.push_back(std::move(slot));
    }

    // Classify the tuple — each element takes the ORDINARY road (class dispatch, literal
    // flex, widen), and a nested aggregate slot explodes in its turn.
    inferExpr(tree, *tup, widen::kNoType, diag);
    widen::TypeRef ty = tup->inferred_type;

    // AN ARRAY IS A HOMOGENEOUS TUPLE — AND IT MUST STAY ONE. The slots are carried in a
    // kTupleExpr, which infers to a TUPLE type: so exploding an ARRAY silently RETYPED the
    // value, and every site downstream then saw a CROSS-FORM copy (array <- tuple) and
    // lowered it by spilling the source to a temp and copying leaf by leaf (desugar's
    // lowerAggCopyStmt). For a class-bearing array that temp costs a ctor and a dtor PER SLOT
    // — copied in, then immediately overwritten by the very operation that built it. Re-form
    // the result as the ARRAY of the slot type (widening may have changed the element type,
    // so it is read off the SLOTS, not off the operand). A nested array slot folds its dims
    // back in, so `int[2][3]` explodes to `int[2][3]`.
    if (all_arrays) {
        std::vector<widen::TypeRef> slots = widen::get(widen::strip(ty)).slots;
        bool homogeneous = !slots.empty();
        for (widen::TypeRef s : slots)
            if (widen::deepStrip(s) != widen::deepStrip(slots[0])) homogeneous = false;
        if (homogeneous) {
            widen::TypeRef S = widen::strip(slots[0]);
            std::vector<int> dims;
            dims.push_back(n);
            widen::TypeRef elem = S;
            if (widen::form(S) == widen::Type::Form::kArray) {
                widen::Type const& st = widen::get(S);
                elem = st.elem;
                for (int d : st.dims) dims.push_back(d);   // snapshot before interning
            }
            ty = widen::internArray(elem, dims);
        }
    }
    tup->inferred_type = ty;

    // SEQ placement for each spill (outermost = evaluated first, so wrap in reverse):
    // the temp is read by THIS expression alone, so desugar hoists it into the statement's
    // seq and it dies at the semicolon.
    std::unique_ptr<parse::Node> result = std::move(tup);
    for (auto it = spills.rbegin(); it != spills.rend(); ++it) {
        result = seqSpill(std::move(*it), std::move(result));
        result->inferred_type = ty;
    }
    e = std::move(*result);
    e.inferred_type = ty;
    return true;
}

// A CLASS CAN ONLY BE COPIED INTO — it has to EXIST first.
//
// So a fresh binding whose initializer contains a same-type class-bearing LVALUE cannot
// simply FILL the storage from that source: the binding site fills and THEN finalizes
// (runs the ctor hooks), so the constructor would land on top of the copied value and
// clobber it. The order a binding must produce is alloc, init, ctor, THEN the transfer.
//
// This peels every such TRANSFER out of the initializer and re-emits it as an assignment
// AFTER the declaration, leaving the DEFAULT value in its place — so the decl constructs a
// proper object (fields defaulted, ctor run) and the transfer copies into it. It is the
// same shape swap-init has always used (default-construct, then re-dispatch as an ordinary
// swap), generalized to `=` and `<--` and, crucially, applied PER SLOT: a CONSTRUCTION slot
// still builds in place (no temp, no copy), and only a copy is deferred. So a mixed literal
// `(C(1), c2)` builds slot 0 and copies into slot 1.
//
// It recurses through tuple / array literals AND a class's field tuple, because a class
// FIELD taking an lvalue is the identical bug one level down (`Holder h( c )`).
// Returns the transfer statements in `post`, in slot order.
void splitTransferInit(parse::Tree& tree, std::unique_ptr<parse::Node>& init,
                       widen::TypeRef type, parse::Node const& path, bool is_move,
                       std::vector<std::unique_ptr<parse::Node>>& post,
                       diagnostic::Sink& diag, bool is_root) {
    if (!init) return;
    widen::TypeRef S = widen::strip(type);
    if (!widen::hasInPlaceClass(S)) return;   // no class in here — nothing to order
    // An op= CONVERSION slot — `(Class = value)`, minted by buildClassFromValue when a user
    // op= accepts a tuple/scalar value — is peeled like any other transfer: the decl
    // default-constructs the slot, then the conversion's own fill runs IN PLACE against it,
    // so a class slot reached through a tuple/array literal op='s with NO `_$cret` temp and
    // NO extra copy. Retarget the fill's receiver from `_$cret` to the slot lvalue and drop
    // the temp's default-construct (the decl supplies the default); applyTransferSplit
    // re-classifies the fill, which re-resolves the op= against the new receiver. Outside a
    // decl the conversion is never peeled — desugar lifts it to a temp (the fallback).
    if (init->kind == parse::Kind::kConvertExpr && init->class_conversion) {
        assert(init->children.size() == 2
            && "class_conversion must carry [construct, fill]");
        auto fill = std::move(init->children[1]);
        fill->children[0] = cloneExpr(path);       // receiver _$cret -> the slot lvalue
        post.push_back(std::move(fill));
        init = classZeroValue(tree, type, path.file_id, path.tok, diag);
        init->inferred_type = type;
        return;
    }
    // A whole-value TRANSFER of THIS storage. A BARE LVALUE is one: an rvalue (a
    // construction, a call) has no object to copy FROM — it BUILDS the storage, which is
    // the elide, and must stay exactly as it is.
    //
    // A class CHAIN (`p + q`) in a SLOT is the one rvalue that must ALSO be peeled, and the
    // reason is a limit of desugar, not of the language: the tuple-literal distribution can
    // hand a slot to a nested literal or to a construction, but NOT to a chain — a chain's
    // accumulator home is answered per STATEMENT, and a slot of a literal is not one. So the
    // chain lifts to a temp, the tuple is formed from the temps, and the whole VALUE fills
    // the storage — after which the slot's ctor runs ON TOP of the copied value, which is
    // exactly the bug this function exists to prevent, reached by a different road (a class
    // whose ctor writes its own field read back 99, not the sum). Peeled, the slot is
    // constructed and the chain is then assigned INTO it — an ordinary chain into a live
    // target, which desugar already lowers with one statement-scoped accumulator, MOVED in.
    // At the ROOT it must NOT be peeled: a decl whose whole rhs is a chain IS the
    // accumulator (zero temps), and that is already correct.
    bool chain_slot = !is_root && init->class_op_chain;
    if (isBareLvalue(*init) || chain_slot) {
        if (!chain_slot) inferExpr(tree, *init, type, diag);
        if (widen::deepStrip(init->inferred_type) == widen::deepStrip(type)) {
            auto st = std::make_unique<parse::Node>();
            st->file_id = init->file_id;
            st->tok = init->tok;
            // The statement shape follows the TARGET: a bare name is a kAssignStmt (target
            // in `name`, rhs in child 0) — kStoreStmt is deref/index-only — while a slot
            // path (`t[0]`, `h.c_`) is a kStoreStmt. A move is a kMoveStmt either way (it
            // already carries its lhs as an expression child) — EXCEPT from a CHAIN, which
            // is an rvalue: a move needs a source OBJECT to husk, and a chain has none (it
            // IS the value). It assigns into the slot, whatever the binding was spelled.
            if (is_move && !chain_slot) {
                st->kind = parse::Kind::kMoveStmt;
                st->children.push_back(cloneExpr(path));
            } else if (path.kind == parse::Kind::kIdentExpr) {
                st->kind = parse::Kind::kAssignStmt;
                st->name = path.name;
                st->name_tok = path.name_tok;
                st->resolved_entry_id = path.resolved_entry_id;
            } else {
                st->kind = parse::Kind::kStoreStmt;
                st->children.push_back(cloneExpr(path));
            }
            st->children.push_back(std::move(init));
            post.push_back(std::move(st));
            init = classZeroValue(tree, type, path.file_id, path.tok, diag);
            init->inferred_type = type;
        }
        return;
    }
    // A tuple / array LITERAL — recurse PER SLOT. A tuple and an array have no constructor
    // of their OWN, so a slot's copy can be deferred past the whole aggregate's construction
    // and still land right after that slot's own ctor.
    //
    // A CLASS's field tuple is NOT recursed, even though a field taking a class lvalue is
    // the same bug one level down. A construction's arguments are FIELD INITIALIZERS, and a
    // constructor must see its fields ALREADY INITIALIZED — `Hook(0, ha, hb)` whose ctor
    // body computes `a_ + b_` has to read the passed values, not the defaults. So a field's
    // copy cannot be hoisted past the enclosing ctor the way a tuple slot's can: it has to
    // land BETWEEN the field's own ctor and the enclosing class's ctor body, which is inside
    // emitConstructed's hook recursion, where the initializer expressions no longer exist.
    // Left as-is (todo.txt): the field is still FILLED and then constructed over.
    if (init->kind != parse::Kind::kTupleExpr) return;
    widen::Type::Form df = widen::form(S);
    if (df != widen::Type::Form::kTuple && df != widen::Type::Form::kArray) return;
    std::vector<widen::TypeRef> slots = initSlotTypes(type);
    if (slots.empty() || slots.size() != init->children.size()) return;
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (!init->children[i]) continue;
        if (!widen::hasInPlaceClass(widen::strip(slots[i]))) continue;
        auto slot_path = makeSlotPath(path, i, slots[i]);
        splitTransferInit(tree, init->children[i], slots[i], *slot_path, is_move, post,
                          diag, /*is_root=*/false);
    }
}

// Peel the TRANSFERS out of a fresh class-bearing decl (splitTransferInit) and re-emit them
// AFTER it. The DECL — now initializing to DEFAULTS wherever a copy used to sit — goes into
// the prelude, and the transfer statements follow it, the last one BECOMING `s` (the same
// idiom desugarDestructure and swap-init use). So the object is fully CONSTRUCTED and only
// then copied into: alloc, init, ctor, op=.
void applyTransferSplit(parse::Tree& tree, parse::Node& s,
                        widen::TypeRef fn_return_type, diagnostic::Sink& diag,
                        std::vector<std::unique_ptr<parse::Node>>* prelude) {
    // A GLOBAL never reaches here with a prelude — its initializer is lowered into a
    // synthesized lazy ctor (desugar), not into statements around the decl — so a global
    // class copy-init still FILLS and then CONSTRUCTS. That is the ordering bug, unfixed,
    // for globals only. todo.txt.
    if (!prelude || s.is_const || s.is_global) return;
    if (s.resolved_entry_id < 0 || s.children.empty() || !s.children[0]) return;
    if (!widen::hasInPlaceClass(widen::strip(s.return_type))) return;
    auto path = std::make_unique<parse::Node>();
    path->kind = parse::Kind::kIdentExpr;
    path->name = s.name;
    path->name_tok = s.name_tok;
    path->resolved_entry_id = s.resolved_entry_id;
    path->inferred_type = s.return_type;
    path->file_id = s.file_id;
    path->tok = s.tok;
    // THE SLOT-WISE EXPLODE wraps its result in an agg_conv_spill SEQ when an operand had to
    // be evaluated exactly once (a call). That seq HIDES the tuple literal from the peel
    // below — and merely looking THROUGH it would be wrong: the spill's decl rides on the
    // decl's rhs, so the temp would die at the DECL's semicolon, before the peeled transfers
    // that read it. Lift the spill decls out and re-place them as a GROUP (see THE SPILL
    // FUNNEL): the decl (now defaults) goes to the prelude, and the spill and the transfers
    // share ONE BLOCK — so the temp still dies at the statement's end, and the transfers can
    // still see it.
    std::vector<std::unique_ptr<parse::Node>> spills;
    while (s.children[0] && s.children[0]->agg_conv_spill
           && s.children[0]->children.size() == 2) {
        auto seq = std::move(s.children[0]);
        spills.push_back(std::move(seq->children[0]));
        s.children[0] = std::move(seq->children[1]);
    }
    std::vector<std::unique_ptr<parse::Node>> post;
    splitTransferInit(tree, s.children[0], s.return_type, *path, s.default_move_init,
                      post, diag, /*is_root=*/true);
    if (post.empty()) {
        // Nothing is copied in — the decl BUILDS its object. Put the seq back exactly as it
        // was (a class-free aggregate keeps its spill on the rhs, where desugar hoists it).
        for (auto it = spills.rbegin(); it != spills.rend(); ++it) {
            widen::TypeRef ty = s.children[0]->inferred_type;
            s.children[0] = seqSpill(std::move(*it), std::move(s.children[0]));
            s.children[0]->inferred_type = ty;
        }
        return;
    }
    auto decl = std::make_unique<parse::Node>();
    decl->kind = parse::Kind::kVarDeclStmt;
    decl->name = s.name;
    decl->name_tok = s.name_tok;
    decl->resolved_entry_id = s.resolved_entry_id;
    decl->return_type = s.return_type;
    decl->file_id = s.file_id;
    decl->tok = s.tok;
    decl->children.push_back(std::move(s.children[0]));
    prelude->push_back(std::move(decl));
    // GROUP placement: a spilled source is read by the transfer STATEMENTS, so it and they
    // share a block — the temp dies at the statement's end, not at the enclosing scope's.
    if (!spills.empty()) {
        std::vector<std::unique_ptr<parse::Node>> body;
        for (auto& sp : spills) body.push_back(std::move(sp));
        for (auto& p : post) {
            classifyStmt(tree, *p, fn_return_type, diag, &body);
            body.push_back(std::move(p));
        }
        s.kind = parse::Kind::kBlockStmt;
        s.name.clear();
        s.resolved_entry_id = -1;
        s.return_type = widen::kNoType;
        s.default_move_init = false;
        s.children.clear();
        for (auto& b : body) s.children.push_back(std::move(b));
        return;
    }
    for (std::size_t k = 0; k + 1 < post.size(); k++) {
        classifyStmt(tree, *post[k], fn_return_type, diag, prelude);
        prelude->push_back(std::move(post[k]));
    }
    auto last = std::move(post.back());
    s.kind = last->kind;
    s.name = last->name;
    s.name_tok = last->name_tok;
    s.resolved_entry_id = last->resolved_entry_id;
    s.return_type = last->return_type;
    s.default_move_init = false;
    s.children = std::move(last->children);
    classifyStmt(tree, s, fn_return_type, diag, prelude);
}

// Does this initializer TRANSFER a class into a SLOT — copy/move an existing object in,
// rather than BUILD one there? A class LVALUE is one; so is a class-producing operator
// CHAIN in a slot (splitTransferInit peels both, and for the same reason). Asked BEFORE
// inferExpr — a chain is not stamped yet, so a binary/unary at a class-bearing slot IS one.
// The RETURN arm asks it of a tuple/array LITERAL: a literal with a transfer in it has to be
// ordered per slot, which needs a NAME to address (`_$ret`).
bool hasClassTransferSlot(parse::Node const& init, widen::TypeRef type) {
    if (!widen::hasInPlaceClass(widen::strip(type))) return false;
    if (isBareLvalue(init)) return true;
    if (init.kind == parse::Kind::kBinaryExpr
        || init.kind == parse::Kind::kUnaryExpr) return true;
    if (init.kind != parse::Kind::kTupleExpr) return false;
    widen::Type::Form f = widen::form(widen::strip(type));
    if (f != widen::Type::Form::kTuple && f != widen::Type::Form::kArray) return false;
    std::vector<widen::TypeRef> slots = initSlotTypes(type);
    if (slots.size() != init.children.size()) return false;
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (init.children[i] && hasClassTransferSlot(*init.children[i], slots[i]))
            return true;
    }
    return false;
}

// Type-infer a statement LIST, splicing any prelude statements a member emits in
// front of the statement that produced them. A class-from-rvalue-aggregate init
// spills its source to a temp local here (see kVarDeclStmt) so the spread sees an
// lvalue — handling rvalue sources exactly like lvalue ones.
void classifyStmtList(parse::Tree& tree,
                      std::vector<std::unique_ptr<parse::Node>>& list,
                      widen::TypeRef fn_return_type, diagnostic::Sink& diag) {
    for (std::size_t i = 0; i < list.size(); i++) {
        if (!list[i]) continue;
        std::vector<std::unique_ptr<parse::Node>> pre;
        classifyStmt(tree, *list[i], fn_return_type, diag, &pre);
        if (!pre.empty()) {
            std::size_t k = pre.size();
            list.insert(list.begin() + i,
                        std::make_move_iterator(pre.begin()),
                        std::make_move_iterator(pre.end()));
            i += k;   // skip the spliced (already-typed) preludes and the stmt itself
        }
    }
}

// THE ONE member-overload gather. Every DECLARED member (method / operator / hook)
// named `name` in `cls`'s class + base chain is a candidate — a candidate is any entry
// that is DEFINED, PURE (an un-overridden virtual slot, bodyless by design), or EXTERNAL
// (declared in an imported header, defined in the sibling TU and bound at link). Whether a
// body was seen in THIS TU is NOT a match criterion; only whether the member is declared.
// The subtlety this folds in: in the DEFINING TU a header declaration and its local
// definition are SEPARATE entries with the same signature (an overload set allows the
// name), so the external declaration is a candidate ONLY when the frame defines none — the
// defined body wins and the pair counts once (no false ambiguity). The most-derived frame
// that names the member shadows its bases. Arity-agnostic: callers filter param_types.size().
//
// This retires three hand-rolled `e.defined`-only scans (findClassOperator,
// classHasOperatorArity, stampClassBinary) that predated cross-TU classes and never grew
// the external arm — so a user operator declared in a header was invisible to its importers.
std::vector<int> declaredMemberOverloads(parse::Tree& tree, widen::TypeRef cls,
                                         std::string const& name) {
    std::vector<int> cands;
    for (int fr : parse::classAndBaseFrames(tree, widen::strip(cls))) {
        if (fr < 0) continue;
        // Every DECLARED member of this name in the frame — defined, pure, OR external.
        std::vector<int> frame;
        for (std::size_t id = 0; id < tree.entries.size(); id++) {
            parse::Entry const& e = tree.entries[id];
            if (e.kind == parse::EntryKind::kFunction && e.owner_ns_frame == fr
                && e.name == name && (e.defined || e.is_pure || e.is_external)
                && e.tmpl_args.empty())   // an INSTANCE shares its template's name —
                frame.push_back((int)id); //   never a candidate (the template is)
        }
        if (frame.empty()) continue;   // name absent here — fall through to a base frame
        // Collapse a decl+def PAIR: in the defining TU a header declaration and its local
        // definition are SEPARATE entries with the same signature — keep the definition,
        // drop the redundant external decl (else the pair reads as an ambiguous overload).
        // Dedup by SIGNATURE, not by "is anything defined": a distinct-signature external
        // is NOT a duplicate — e.g. a user `op=(int)` beside the synthesized default
        // `op=(Self^)` (which is `defined` in every TU). The old coarse "external only if
        // nothing defined" dropped that user op= and made it invisible to importers.
        for (int id : frame) {
            parse::Entry const& e = tree.entries[id];
            if (e.is_external) {
                bool covered = false;
                for (int jd : frame)
                    if (jd != id && !tree.entries[jd].is_external
                        && tree.entries[jd].param_types == e.param_types) {
                        covered = true; break;
                    }
                if (covered) continue;
            }
            cands.push_back(id);
        }
        break;   // most-derived frame with the name shadows its bases
    }
    return cands;
}

// A method call whose sole same-name member is a TEMPLATE: bind the type-list
// through the shared binder (receiver slot skipped) and instantiate. Returns the
// instance entry id (-1 = reported); the caller proceeds exactly as with a plain
// single method (defaults, conversions, the coercion retry — all the existing
// member machinery).
int classifyTemplateMethodCall(parse::Tree& tree, parse::Node& s, int tid,
                               std::vector<parse::Node*> const& uargs,
                               diagnostic::Sink& diag) {
    std::vector<widen::TypeRef> bound;
    if (!bindTemplateTypeList(tree, s, tid, uargs, /*recv_offset=*/1, s.name_tok,
                              diag, bound)) {
        // A method's explicit-arity mismatch has no resolve-side check (the callee
        // binds off the receiver's class, only known here) — report it now.
        auto it = tree.templates.find(tid);
        if (it != tree.templates.end() && !s.tmpl_args.empty()
            && s.tmpl_args.size() != it->second.def->type_params.size()
            && !diagnostic::hasErrors(diag)) {
            diagnostic::report(diag, {s.file_id, s.name_tok,
                "Wrong number of template arguments for '" + s.name + "': "
                + std::to_string(it->second.def->type_params.size())
                + " expected, got " + std::to_string(s.tmpl_args.size()) + ".", {}});
        }
        return -1;
    }
    return instantiateAndClassify(tree, tid, bound, s, diag);
}

void inferMethodCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    // obj.method(args) — infer the receiver, resolve the method on its class's
    // member frame, arity-check + infer the args. desugar reads the receiver's
    // class off children[0].inferred_type to mint the symbol. children =
    // [receiver, args...]; the statement form discards the value, the expression
    // form uses inferred_type.
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
    // Gather the same-name method OVERLOAD SET in the receiver's class frame. A
    // same-name non-method member never enters the set (a method is a kFunction).
    // Gather the method's overload set from the receiver's class frame AND its base
    // chain — the first frame (most-derived) that has the name shadows the rest (a
    // derived overload set hides the base's). A method found in a BASE frame runs on
    // the receiver's base sub-object (offset 0, so the same address).
    std::vector<int> cands = declaredMemberOverloads(tree, rs, s.name);
    if (cands.empty()) {
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Class '" + widen::spell(rs) + "' has no method '" + s.name + "'.", {}});
        return;
    }
    // User args = children[1..] (the receiver children[0] is held out — params[0]
    // is the `_$recv` receiver, always the object). recv_offset = 1.
    std::vector<parse::Node*> uargs;
    for (std::size_t i = 1; i < s.children.size(); i++) uargs.push_back(s.children[i].get());

    // TEMPLATE candidates: the ownership rule keeps templates and plain
    // methods apart, so the set is all-template or all-plain. ARITY-ONLY
    // OVERLOADING: same-name method templates carry disjoint arity ranges —
    // the user-arg count selects the one candidate.
    std::vector<int> tcands;
    for (int c0 : cands) {
        if (tree.entries[c0].is_template) tcands.push_back(c0);
    }

    // Explicit type arguments only make sense on a template method.
    if (!s.tmpl_args.empty() && tcands.empty()) {
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "'" + s.name + "' is not a template method.", {}});
        return;
    }

    int methodId;
    if (!tcands.empty()) {
        int pick = -1;
        for (int c0 : tcands) {
            int lo, hi;
            resolve::templateArityRange(tree, c0, lo, hi);
            if ((int)uargs.size() >= lo && (int)uargs.size() <= hi) {
                pick = c0;
                break;
            }
        }
        if (pick < 0) {
            diagnostic::report(diag, {s.file_id, s.name_tok,
                "Wrong number of arguments to template method '" + s.name
                + "': no overload takes "
                + std::to_string(uargs.size()) + ".", {}});
            return;
        }
        // Bind the type-list — explicit, or unified from the USER args
        // against the patterns behind the receiver slot — instantiate, and
        // proceed exactly as with a plain single method.
        methodId = classifyTemplateMethodCall(tree, s, pick, uargs, diag);
        if (methodId < 0) return;
    } else if (cands.size() == 1) {
        // Single method: keep the detailed arity error (a RANGE now — defaults make
        // trailing user params optional). Args are type-checked below with context.
        parse::Entry const& m = tree.entries[cands[0]];
        std::size_t umin = m.num_required > 1 ? (std::size_t)(m.num_required - 1) : 0;
        std::size_t umax = m.param_types.empty() ? 0 : m.param_types.size() - 1;
        if (uargs.size() < umin || uargs.size() > umax) {
            std::string want = (umin == umax) ? std::to_string(umin)
                : std::to_string(umin) + " to " + std::to_string(umax);
            diagnostic::report(diag, {s.file_id, s.name_tok,
                "Method '" + s.name + "' expects " + want + " arguments, got "
                + std::to_string(uargs.size()) + ".", {}});
            return;
        }
        methodId = cands[0];
    } else {
        // 2+ overloads: infer the provided args without context, then rank by cost
        // through the shared engine (receiver excluded via recv_offset 1).
        for (auto* a : uargs) if (a) inferExpr(tree, *a, widen::kNoType, diag);
        methodId = pickOverload(tree, cands, uargs, /*recv_offset=*/1,
                                s.name, s.file_id, s.name_tok, diag);
        if (methodId < 0) return;
    }

    // Thread the chosen method onto the node — desugar mints the symbol from the
    // method's OWN defining class. children = [receiver, args...] and param_types =
    // [_$recv, user...] are receiver-aligned, so fillDefaults appends omitted
    // trailing defaults at the natural index.
    s.resolved_entry_id = methodId;
    parse::Entry const& m = tree.entries[methodId];
    s.param_types = m.param_types;    // [self, user...] — emitCall arity check
    s.return_type = m.slids_type;     // emitCall reads return_type
    s.inferred_type = m.slids_type;
    fillDefaults(s, m);
    for (std::size_t i = 1; i < s.children.size(); i++) {
        if (!s.children[i]) continue;
        // Type-check each arg against its param (s.param_types is a stable copy; an
        // inferExpr below may realloc tree.entries and dangle a live entry ref).
        inferExpr(tree, *s.children[i], s.param_types[i], diag);
        // The single-method path never reaches pickOverload, so the coercion retry lives
        // here too (see classifyCall's twin). It also covers the class OPERATORS that are
        // rewritten into method calls — a comparison `a == x`, an index `a[x]` — so those
        // families get the rule without a bespoke arm. An arg pickOverload already coerced
        // is now the class, and coerceOperandToClass declines it, so there is no second wrap.
        std::size_t before = diag.records.size();
        checkArgAssign(tree, s.param_types[i], *s.children[i], diag);
        if (diag.records.size() != before) {
            diag.records.resize(before);
            widen::TypeRef want = classParamTarget(s.param_types[i]);
            if (want != widen::kNoType)
                coerceOperandToClass(tree, want, *s.children[i], diag);
            checkArgAssign(tree, s.param_types[i], *s.children[i], diag);
        }
    }
}

bool isArithBitBinaryOp(std::string const& op) {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%"
        || op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>";
}

// True if `cls` (or a base) defines a method `opname` taking exactly `nUserParams`
// user parameters (i.e. param_types.size() == nUserParams + 1, the receiver first).
bool classHasOperatorArity(parse::Tree& tree, widen::TypeRef cls,
                           std::string const& opname, std::size_t nUserParams) {
    for (int id : declaredMemberOverloads(tree, cls, opname))
        if (tree.entries[id].param_types.size() == nUserParams + 1)
            return true;
    return false;
}

// Probe for the best-matching single-parameter operator `opname` in `cls` (or its
// base chain) for the argument `rhs`. Gathers candidates (most-derived frame with the
// name shadows bases) then ranks them through the SHARED rankOverload core. Returns
// the winning entry id, -1 if NONE match (the caller reports "no operator" / falls to
// its own error path), or -2 on a genuine tie, which is reported HERE (with each
// conflicting declaration cited) — the distinct sentinel lets a caller suppress its
// own no-operator message. `rhs` must already be inferred.
int findClassOperator(parse::Tree& tree, widen::TypeRef cls, parse::Node const& rhs,
                      std::string const& opname, diagnostic::Sink& diag) {
    std::vector<int> cands;
    for (int id : declaredMemberOverloads(tree, cls, opname))
        if (tree.entries[id].param_types.size() == 2)   // [_$recv, one user param]
            cands.push_back(id);
    // recv_offset = 1: `_$recv` is implicit, so only the single user operand `rhs` is
    // ranked (an assignment operator is arity-1). A cast drops rhs's const only for the
    // read-only argConvertCost scoring inside the shared core.
    std::vector<parse::Node*> args{const_cast<parse::Node*>(&rhs)};
    OverloadRank r = rankOverload(tree, cands, args, 1);
    if (r.tied.size() > 1) {
        reportAmbiguity(tree, rhs.file_id, rhs.tok,
            "Ambiguous operator '" + opname + "': more than one overload of class '"
            + widen::spellOrEmpty(cls) + "' matches source type '"
            + widen::spellOrEmpty(rhs.inferred_type) + "' equally well.",
            r.tied, 1, diag);
        return -2;
    }
    return r.best;
}

// v1 PHASE-2 OPERAND COERCION (ONE level) — the operand half, shared by every class-operator
// dispatch family. When an operand is accepted by no overload of the operator directly but IS
// op='able into the class (an `int` reaching `op=(int64)` by widening), wrap it in a
// `(C = operand)` conversion: the class-conversion path default-constructs a C temp, dispatches
// op= from the source, and yields the temp (desugar lifts it). The operand is now a C VALUE, so
// the C-operators apply — and being a C it can never re-coerce, which is what bounds this to ONE
// level. Reuses lowerClassConversion wholesale: no new lowering, no desugar change, one
// countable temp dead at the semicolon.
//
// ONLY THE WRAP LIVES HERE. Each caller re-probes its own operator afterwards, because what to
// re-probe differs per family (op<OP> and op<OP>= for a binary, op<OP>= for a compound assign,
// op<CMP> for a comparison). One place decides what a class can be built FROM; the families
// decide what to do with the answer. This used to be a lambda closed over stampClassBinary's
// candidate ids, which is why `a + i` coerced and `a += i` did not — the rule was a fact about
// one call site instead of about the type system.
//
// NOT wired to `op<--` / `op<-->` (they share dispatchAssignInit, so exclusion is deliberate):
// a move from a freshly built temp is merely pointless, but a SWAP with one silently discards
// the result — the compiler would be manufacturing a bug rather than obeying the author.
//
// Rewrites the operand NODE IN PLACE (it becomes the conversion, wrapping what it was), so a
// caller holding only a `parse::Node&` — an entry in an overload ranker's `args` vector, say —
// can coerce without owning the parent's slot.
bool coerceOperandToClass(parse::Tree& tree, widen::TypeRef cls,
                          parse::Node& operand, diagnostic::Sink& diag) {
    widen::TypeRef cs = widen::strip(cls);
    if (widen::form(cs) != widen::Type::Form::kSlid) return false;
    auto itc = tree.classes.find(cs);
    if (itc == tree.classes.end()) return false;
    // Already the class: nothing to build, and the ONE-level bound depends on this.
    if (widen::deepStrip(operand.inferred_type) == widen::deepStrip(cls)) return false;
    // Probe op= WITHOUT reporting — a miss is not an error here, it just means no coercion is
    // available and the caller falls to its own diagnostic. An AMBIGUOUS op= (-2) is left alone
    // for the same reason: this is a probe, and the caller owns what the author sees.
    std::size_t before = diag.records.size();
    int eq_id = findClassOperator(tree, cs, operand, "op=", diag);
    diag.records.resize(before);
    if (eq_id < 0) return false;
    auto inner = std::make_unique<parse::Node>(std::move(operand));
    operand = parse::Node{};
    operand.kind = parse::Kind::kConvertExpr;
    operand.return_type = itc->second.type;
    operand.file_id = inner->file_id;
    operand.tok = inner->tok;
    operand.children.push_back(std::move(inner));
    inferExpr(tree, operand, widen::kNoType, diag);
    return true;
}

// Rewrite an assignment-form statement `lvalue = rhs` (a bare-name kAssignStmt) into
// the method call `lvalue.opname(rhs)` IN PLACE, then resolve it via inferMethodCall.
// Reuses the whole method-call path (resolution here, lowerMethodCall in desugar) so
// a user assignment operator needs no bespoke lowering. `s.name`/`resolved_entry_id`
// name the lvalue variable; `s.children[0]` is the rhs.
void rewriteAssignToOperatorCall(parse::Tree& tree, parse::Node& s,
                                 widen::TypeRef lref, std::string const& opname,
                                 diagnostic::Sink& diag) {
    auto recv = std::make_unique<parse::Node>();
    recv->kind = parse::Kind::kIdentExpr;
    recv->name = s.name;
    recv->name_tok = s.name_tok;
    recv->resolved_entry_id = s.resolved_entry_id;
    recv->inferred_type = lref;
    recv->file_id = s.file_id;
    recv->tok = s.tok;

    std::unique_ptr<parse::Node> rhs = std::move(s.children[0]);
    s.kind = parse::Kind::kMethodCallStmt;
    s.name = opname;
    s.name_tok = s.tok;
    s.resolved_entry_id = -1;
    s.children.clear();
    s.children.push_back(std::move(recv));
    s.children.push_back(std::move(rhs));
    inferMethodCall(tree, s, diag);
}

// Rewrite a statement whose lhs is already an EXPRESSION child (kMoveStmt / kSwapStmt
// hold children [lhs, rhs] — exactly the method-call shape) into `lhs.opname(rhs)`.
void rewriteExprLhsToOperatorCall(parse::Tree& tree, parse::Node& s,
                                  std::string const& opname, diagnostic::Sink& diag) {
    s.kind = parse::Kind::kMethodCallStmt;
    s.name = opname;
    s.name_tok = s.tok;
    s.resolved_entry_id = -1;
    inferMethodCall(tree, s, diag);
}

// THE ONE FUNNEL binding a value into a target when the target's class defines a matching
// user operator. EVERY binding site routes here so none can silently skip the check (that
// scatter is how the kStoreStmt op= hole got in): the 5 live-storage assignment forms
// (kAssignStmt, kStoreStmt, kMoveStmt, kSwapStmt, kAugAssignStmt) AND both decl-init shapes
// (typed `Class x = e`, inferred `x = e`). `clsType` = the target's type; `rhs` = the
// source (for the overload probe); `opname` = the operator; `expr_lhs` = the target is an
// EXPRESSION child ([lhs, rhs], idx 1) vs a BARE NAME (in s.name, rhs idx 0). Returns true
// iff it rewrote `s` into the operator method call (else the caller does the default
// copy/move/elide). `rhs` must already be inferred (findClassOperator ranks by its type).
//
// Two knobs let a DECL target share the funnel (the old "decl-init can't route here" seam):
//  * a class RVALUE source (a call / op / construction result) has no address for the
//    operator's `Class^` param, so it is materialized into a `_$cinit` temp built by the
//    DEFAULT copy/elision (classifyClassInit — NOT the operator, so no recursion), and the
//    source child is replaced by a bare ident to it. A construction source is restructured
//    to its ctor-arg tuple first (field-init, not a raw-ctor copy codegen can't emit). Both
//    need a `prelude` to splice the temp into; a named lvalue / primitive source is untouched.
//  * `needs_construct` (a FRESH decl target) splices a default-construct of the target into
//    the prelude BEFORE the rewrite reads s.name — so the fresh var is constructed THEN op'd.
bool dispatchAssignInit(parse::Tree& tree, parse::Node& s, widen::TypeRef clsType,
                        parse::Node& rhs, std::string const& opname, bool expr_lhs,
                        diagnostic::Sink& diag,
                        std::vector<std::unique_ptr<parse::Node>>* prelude = nullptr,
                        bool needs_construct = false) {
    if (widen::form(widen::strip(clsType)) != widen::Type::Form::kSlid) return false;
    int op_id = findClassOperator(tree, clsType, rhs, opname, diag);
    if (op_id < 0) return false;
    // A SYNTHESIZED op dispatches exactly like a USER one — the operator is the operator,
    // whoever wrote it. (It used to bail: "the codegen transfer path already calls
    // @Class__$copy, so let it". That was true for a plain same-type copy, and it also meant
    // a class RVALUE source — a hook-returning call, a construction — could never reach the
    // funnel's `_$cinit` spill unless the author happened to have written an op=. So
    // `arr[0] = mkClass()` was rejected for a default-copy class and accepted for a class
    // with a user op=, which is not a distinction the language makes.)
    int idx = expr_lhs ? 1 : 0;
    // A stamped class-operator CHAIN of the target's EXACT class is lowered by desugar,
    // which builds the accumulator IN the target (a fresh one) or in a statement-scoped
    // temp it then MOVES in (a live one). Spilling it to a `_$cinit` here would materialize
    // the very object the chain lowering exists to avoid, and dispatching op= on the spill
    // would copy it a second time. Bail: the statement keeps the raw chain as its source.
    // A chain of a DIFFERENT class still routes through the funnel — that IS a conversion.
    if (idx < static_cast<int>(s.children.size()) && s.children[idx]
        && s.children[idx]->class_op_chain
        && widen::deepStrip(s.children[idx]->inferred_type) == widen::deepStrip(clsType))
        return false;
    // ELIDE-WHENEVER-POSSIBLE (return-value site canon): a FRESH decl target
    // (needs_construct) initialized from a same-type class RVALUE builds in place —
    // RVO / field-init, NO operator — even though `=`/`<--` syntax is used and a user
    // op= / op<-- exists. Elision needs an rvalue (a call / construction / op temp): a
    // live LVALUE source is a genuine copy (dispatch op=), and a NON-EXACT source is a
    // convert (dispatch op= + widen). Bail so the caller's construction/elision path
    // runs. An existing-var assign (needs_construct=false) is unaffected — case 3/4
    // always spill + op. The author forces the operator by declaring, then assigning.
    if (needs_construct && idx < static_cast<int>(s.children.size())
        && s.children[idx]) {
        bool src_lvalue = isBareLvalue(*s.children[idx]);
        bool exact = widen::deepStrip(s.children[idx]->inferred_type)
                  == widen::deepStrip(clsType);
        if (!src_lvalue && exact) return false;
    }
    // Materialize a class RVALUE source into a `_$cinit` temp so the operator takes its
    // address (a named lvalue / primitive is used in place).
    // A CONSTRUCTION into LIVE storage (`x = Class(11)`, `arr[0] = Class(11)`, `x <-- ...`)
    // used to bail here, because a live target has no fresh slot to BUILD into — the target
    // is already an object, and field-initializing over it would skip its ctor. That is the
    // reason for the ELIDE, not a reason to reject: what a live target wants is a TRANSFER,
    // and the source just has to be materialized somewhere first. The `_$cinit` spill below
    // does exactly that (it restructures a construction into its field tuple and builds a
    // temp from it), so the construction becomes a proper object and the target is copied /
    // moved into through its operator. The temp dies at the semicolon.
    // A class RVALUE source is NOT spilled here. It used to be — into a `_$cinit` local
    // spliced into the prelude — and that gave the temp ENCLOSING-BLOCK lifetime: the temp
    // in `x = C(11);` outlived the semicolon and was destroyed at the end of the block.
    // Left in place, the rvalue is just an ARGUMENT of the operator call this rewrites to,
    // and desugar already lifts a construction / hook-returning call in an argument into a
    // statement-scoped temp (liftSretCallExprs, block-wrapped by liftSretCallList) — the
    // same path `fn(Class(1))` takes. So the operator gets its address and the temp dies at
    // the semicolon, which is what a temporary is supposed to do.
    // A FRESH decl target is default-constructed before the operator runs (splice lands in
    // the prelude, before this stmt becomes `name.op(...)`); live storage already exists.
    if (needs_construct && prelude) {
        auto itc = tree.classes.find(widen::strip(clsType));
        if (itc != tree.classes.end()) {
            auto dc = std::make_unique<parse::Node>();
            dc->kind = parse::Kind::kVarDeclStmt;
            dc->name = s.name;
            dc->name_tok = s.name_tok;
            dc->resolved_entry_id = s.resolved_entry_id;
            dc->return_type = clsType;
            dc->file_id = s.file_id;
            dc->tok = s.tok;
            classifyClassInit(tree, *dc, itc->second, diag);
            prelude->push_back(std::move(dc));
        }
    }
    if (expr_lhs) rewriteExprLhsToOperatorCall(tree, s, opname, diag);
    else          rewriteAssignToOperatorCall(tree, s, clsType, opname, diag);
    return true;
}

// Build and type (via inferMethodCall) the statement `recv.opname(args...)`.
std::unique_ptr<parse::Node> makeOpCallStmt(
    parse::Tree& tree, std::string const& recvName, int recvEid, widen::TypeRef recvType,
    std::string const& opname, std::vector<std::unique_ptr<parse::Node>> args,
    int file, int tok, diagnostic::Sink& diag) {
    auto call = std::make_unique<parse::Node>();
    call->kind = parse::Kind::kMethodCallStmt;
    call->name = opname;
    call->name_tok = tok;
    call->file_id = file;
    call->tok = tok;
    call->resolved_entry_id = -1;
    auto recv = std::make_unique<parse::Node>();
    recv->kind = parse::Kind::kIdentExpr;
    recv->name = recvName;
    recv->name_tok = tok;
    recv->resolved_entry_id = recvEid;
    recv->inferred_type = recvType;
    recv->file_id = file;
    recv->tok = tok;
    call->children.push_back(std::move(recv));
    for (auto& a : args) call->children.push_back(std::move(a));
    inferMethodCall(tree, *call, diag);
    return call;
}

// Lower a class-target conversion `(Class = src)` — "assignment to a temporary" — into
// the construct-then-op= form the desugar lift hoists: a default-construct `_$cret` decl
// plus the `_$cret.op=(src)` dispatch, stashed as the node's two children with
// class_conversion set (desugar lifts both into a temp and yields a read of it). The
// class must define an op= accepting the source; otherwise a clean diagnostic fires
// (the source is not a viable conversion). Reuses findClassOperator + classifyClassInit
// + makeOpCallStmt — the same machinery a decl-init op= uses; no bespoke lowering.
void lowerClassConversion(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    widen::TypeRef C = e.return_type;
    e.inferred_type = C;
    parse::Node& operand = *e.children[0];
    // The conversion is "assignment to a temp": it dispatches the target's op= exactly as
    // a decl-init `Class x = src` would. findClassOperator surfaces every op= — a user
    // op=(T)/op=(T^) AND the compiler-synthesized default op=(Self^), so a same-class
    // source resolves to the memberwise default copy with no bespoke fallback here. Only a
    // source no op= accepts is a clean error.
    int op_id = findClassOperator(tree, C, operand, "op=", diag);
    if (op_id == -2) return;   // ambiguous op= — findClassOperator already reported
    if (op_id < 0) {
        diagnostic::report(diag, {e.file_id, e.tok,
            "Cannot convert '" + widen::spellOrEmpty(operand.inferred_type)
            + "' to class '" + widen::spellOrEmpty(C)
            + "'; the class has no assignment operator accepting the source.", {}});
        return;
    }
    auto itc = tree.classes.find(widen::strip(C));
    // A viable op= (or a same-class copy) implies C is a known class — its entry cannot
    // be absent here. Assert the invariant rather than silently returning (which would
    // leave an un-lowered kConvertExpr for codegen to choke on).
    assert(itc != tree.classes.end()
        && "lowerClassConversion: class absent from tree.classes");
    int f = e.file_id, tk = e.tok;
    // Mint the `_$cret` temp entry (the conversion's result value).
    parse::Entry ce;
    ce.kind = parse::EntryKind::kLocalVar;
    ce.name = "_$cret";
    ce.slids_type = widen::strip(C);
    ce.file_id = f;
    ce.tok = tk;
    int cid = static_cast<int>(tree.entries.size());
    tree.entries.push_back(std::move(ce));
    // Default-construct decl (classifyClassInit builds the per-field default tuple).
    auto dc = std::make_unique<parse::Node>();
    dc->kind = parse::Kind::kVarDeclStmt;
    dc->name = "_$cret";
    dc->name_tok = tk;
    dc->resolved_entry_id = cid;
    dc->return_type = C;
    dc->file_id = f;
    dc->tok = tk;
    classifyClassInit(tree, *dc, itc->second, diag);
    // The second lifted statement fills `_$cret` from the source by dispatching
    // `_$cret.op=(src)` (the operand is the operator's argument) — a user op= OR the
    // synthesized default copy, both real methods. This is one statement in the
    // [construct, fill] pair the desugar class_conversion lift hoists.
    std::vector<std::unique_ptr<parse::Node>> args;
    args.push_back(std::move(e.children[0]));
    auto fill = makeOpCallStmt(tree, "_$cret", cid, C, "op=", std::move(args), f, tk, diag);
    e.class_conversion = true;
    e.resolved_entry_id = cid;
    e.children.clear();
    e.children.push_back(std::move(dc));
    e.children.push_back(std::move(fill));
}

// THE class-from-VALUE funnel. A class slot is built from a value in one of two ways,
// and the choice belongs in ONE place: if a USER op= accepts the value's type, the value
// is ASSIGNED (default-construct + op=, the `(Class = value)` conversion lowered by
// lowerClassConversion) — else its elements are SPREAD as a field list (constructClass).
// This is the expression-position twin of the statement-position dispatchAssignInit, so a
// tuple SLOT / array ELEMENT / class FIELD reaches a user op= exactly as a top-level
// `Class c = value` decl does. A value already of THIS class is a copy, NOT a conversion:
// left to constructClass (a whole-value store honouring the transfer invariant), never an
// op= that would double-construct. A FIELD-LIST construction (`Class c(args)`, a base's
// flat splice) never routes here — it calls constructClass directly.
std::unique_ptr<parse::Node> buildClassFromValue(parse::Tree& tree,
                                                 parse::ClassInfo const& info,
                                                 std::unique_ptr<parse::Node> init,
                                                 int file_id, int tok,
                                                 diagnostic::Sink& diag,
                                                 bool subobject) {
    if (init) {
        // Infer once to probe for a matching op=. constructClass / classifyClassInit
        // re-infer each field slot with its field-type context, so this context-free
        // pre-inference is harmless for the field-list fall-through.
        inferExpr(tree, *init, widen::kNoType, diag);
        widen::TypeRef vt = init->inferred_type;
        if (vt != widen::kNoType
            && widen::deepStrip(vt) != widen::deepStrip(info.type)
            && findClassOperator(tree, info.type, *init, "op=", diag) >= 0) {
            auto conv = std::make_unique<parse::Node>();
            conv->kind = parse::Kind::kConvertExpr;
            conv->return_type = info.type;
            conv->file_id = file_id;
            conv->tok = tok;
            conv->children.push_back(std::move(init));
            inferExpr(tree, *conv, widen::kNoType, diag);   // -> lowerClassConversion
            return conv;
        }
    }
    // Field-list fall-through: the class itself has no op= for this value. This is the ONE
    // origin of value-init context — every caller of buildClassFromValue is a value-position
    // `= value` site — so mark the field-list `value_init` so a nested class field that DOES
    // have a matching op= is diagnosed rather than silently field-listed.
    return constructClass(tree, info, std::move(init), file_id, tok, diag, subobject,
                          /*value_init=*/true);
}

// STAMP a class-PRODUCING binary `a op b` — RESOLVE it, but do NOT lower it.
//
// The RESULT class is the LHS OPERAND's class — full stop. A binary operator's first
// parameter IS the enclosing class (validateOperatorSignatureTypes), so `a op b` can only
// ever resolve to a's class, and it produces that class (self). There is nothing to pick.
//
// This is deliberate: the expected type (the `=` target) MUST NOT steer operator selection.
// `b + c` has a meaning fixed by its own operands, BEFORE the `=` in `a = b + c` is
// consulted — that is what precedence means. The old context-wins arm (which enabled a
// cross-class `Mat m = v1 + v2`) inverted that, and the rhs-operand arm silently supported
// reversed operands; both are gone. A non-class lhs simply returns false and falls to the
// caller's numeric path (so `og = m + n` on ints is plain int arithmetic, then `op=`).
//
// What is recorded (parse::Node): the four operator candidates a chain lowering can call —
// the 2-arg `op<OP>(lhs, rhs)`, the 1-arg fuse `op<OP>=(rhs)`, and the two 1-arg seeds
// `op=(lhs)` / `op=(rhs)` — plus, as children[2], the result class's DEFAULT field-init
// tuple (the value an accumulator is constructed from). All of that is RESOLUTION, which is
// this pass's job. WHICH candidates fire, and WHERE the accumulator lives, is decided in
// DESUGAR: the elide (letting the destination BE the accumulator) needs the chain and its
// destination together, and only a whole statement carries both.
//
// The node's ROLE in a chain is visible right here, bottom-up, from its own lhs — a
// CONTINUATION (lhs is an already-stamped chain of C), a COLLAPSED HEAD (lhs is a fresh
// `C(...)` construction, which becomes the accumulator itself), or a REAL-OPERAND HEAD.
// Each role needs a different subset of the candidates, so viability is checked per role
// and a class with no viable ladder simply returns false — the caller's existing
// "Operator 'X' is not defined on class 'Y'." then fires, unchanged.
//
// Returns true iff it stamped. Shared by the arith/bitwise hook, the shift arm and the
// logical arm so all three dispatch identically.
bool stampClassBinary(parse::Tree& tree, parse::Node& e, std::string const& op,
                      widen::TypeRef lref, diagnostic::Sink& diag) {
    if (e.class_op_chain) return true;    // inferExpr may revisit a node; stamp once
    widen::TypeRef C = widen::strip(lref);
    if (widen::form(C) != widen::Type::Form::kSlid) return false;
    auto itc = tree.classes.find(C);
    if (itc == tree.classes.end()) return false;
    parse::Node& lhs = *e.children[0];
    parse::Node& rhs = *e.children[1];

    // The four candidates. findClassOperator ranks by the ARGUMENT's inferred type (both
    // operands are already inferred here) and returns -1 when nothing matches.
    std::string opname = "op" + op;
    int bin_id = -1;
    if (classHasOperatorArity(tree, C, opname, 2)) {
        // A 2-arg operator is ranked over BOTH operands, so it needs the full overload
        // core rather than findClassOperator's arity-1 probe.
        std::vector<int> cands;
        for (int id : declaredMemberOverloads(tree, C, opname))
            if (tree.entries[id].param_types.size() == 3)   // [_$recv, lhs, rhs]
                cands.push_back(id);
        std::vector<parse::Node*> args{&lhs, &rhs};
        OverloadRank r = rankOverload(tree, cands, args, 1);
        if (r.tied.size() > 1) {
            reportAmbiguity(tree, e.file_id, e.tok,
                "Ambiguous operator '" + op + "': more than one overload of class '"
                + widen::spellOrEmpty(C) + "' matches the operands equally well.",
                r.tied, 1, diag);
            return false;
        }
        bin_id = r.best;
    }
    int aug_id    = findClassOperator(tree, C, rhs, opname + "=", diag);
    int eq_lhs_id = findClassOperator(tree, C, lhs, "op=", diag);
    int eq_rhs_id = findClassOperator(tree, C, rhs, "op=", diag);
    // The move INTO a live target. Its source is the accumulator — a value of C — so probe
    // with a stand-in of that type. Resolving it HERE is what lets desugar CALL the class's
    // move operator by name: a transfer synthesized after classify never passes through the
    // binding funnel, so it must carry its operator with it. Every class has one (the user's
    // op<--(Self^), else resolve's synthesized default), so this always resolves.
    parse::Node self_val;
    self_val.kind = parse::Kind::kIdentExpr;
    self_val.inferred_type = itc->second.type;
    self_val.file_id = e.file_id;
    self_val.tok = e.tok;
    int move_id = findClassOperator(tree, C, self_val, "op<--", diag);
    if (aug_id    < 0) aug_id    = -1;    // -2 (ambiguous, already reported) reads as absent
    if (eq_lhs_id < 0) eq_lhs_id = -1;
    if (eq_rhs_id < 0) eq_rhs_id = -1;
    if (move_id   < 0) move_id   = -1;

    // v1 PHASE-2 OPERAND COERCION (ONE level). When the RHS operand is accepted by no
    // operator directly (aug_id/bin_id < 0) but IS op='able into C (eq_rhs_id >= 0 — e.g.
    // an `int` into `op=(int64)` via widening), wrap it in a `(C = rhs)` conversion: the
    // class-conversion path default-constructs a C temp, dispatches op= from the source,
    // and yields the temp (desugar lifts it). The rhs is now a C VALUE, so the C-operators
    // (op+=(C^) / op+(C^, C^)) apply — and it is a C, so it never re-coerces. Fires only in
    // the continuation / real-operand roles below (a collapsed head SEEDS via op= instead).
    auto coerceRhsToClass = [&]() -> bool {
        if (!coerceOperandToClass(tree, C, *e.children[1], diag)) return false;
        parse::Node& nrhs = *e.children[1];
        aug_id = findClassOperator(tree, C, nrhs, opname + "=", diag);
        if (aug_id < 0) aug_id = -1;
        bin_id = -1;
        if (classHasOperatorArity(tree, C, opname, 2)) {
            std::vector<int> cands2;
            for (int id : declaredMemberOverloads(tree, C, opname))
                if (tree.entries[id].param_types.size() == 3) cands2.push_back(id);
            std::vector<parse::Node*> args2{&lhs, &nrhs};
            OverloadRank r2 = rankOverload(tree, cands2, args2, 1);
            if (r2.tied.size() <= 1) bin_id = r2.best;
        }
        return true;
    };

    // Role, read straight off the lhs — bottom-up, operand-keyed, never target-keyed.
    bool continuation = lhs.class_op_chain
        && widen::deepStrip(lhs.inferred_type) == widen::deepStrip(C);
    bool collapsed_head = lhs.kind == parse::Kind::kCallExpr && lhs.is_construction
        && widen::deepStrip(lhs.inferred_type) == widen::deepStrip(C);
    bool viable;
    if (continuation) {
        // Fuse with op<OP>=, or (no op<OP>=) start a fresh buffer via the 2-arg op<OP>.
        viable = aug_id >= 0 || bin_id >= 0;
        if (!viable && coerceRhsToClass())
            viable = aug_id >= 0 || bin_id >= 0;
    } else if (collapsed_head) {
        // The construction IS the accumulator; the first operand applies to it. A head
        // built WITH args must use op<OP>= (op= would discard those args).
        viable = lhs.ctor_no_args ? (eq_rhs_id >= 0 || aug_id >= 0) : (aug_id >= 0);
        // THE COLLAPSE IS AN OPTIMIZATION, NOT A REPRESENTATION — so it can be DECLINED. It
        // saves one temp by building the head construction straight into the accumulator, but
        // an accumulator can only be advanced by an operator that MUTATES it with one argument
        // (op<OP>=). A class with only the 2-arg op<OP> has none — and the chain used to give
        // up there and report the operator UNDEFINED, though it is defined and the identical
        // rvalue works both as a CALL head (`mk() + a`) and as the right operand (`a + C(7)`).
        // Decline the collapse instead: the construction becomes an ORDINARY rvalue operand,
        // materialized like any other, and the 2-arg operator is called.
        if (!viable && bin_id >= 0) {
            collapsed_head = false;
            viable = true;
        }
    }
    if (!continuation && !collapsed_head) {
        // A real operand pair: one 2-arg call, or seed-then-fuse.
        viable = bin_id >= 0 || (eq_lhs_id >= 0 && aug_id >= 0);
        if (!viable && coerceRhsToClass())
            viable = bin_id >= 0 || (eq_lhs_id >= 0 && aug_id >= 0);
    }
    if (!viable) return false;

    // Park the accumulator's construction value: the class's DEFAULT field-init tuple,
    // built by the ONE construction funnel (classifyClassInit) so a chain accumulator is
    // constructed exactly like any other default-constructed local.
    auto dc = std::make_unique<parse::Node>();
    dc->kind = parse::Kind::kVarDeclStmt;
    dc->return_type = itc->second.type;
    dc->file_id = e.file_id;
    dc->tok = e.tok;
    classifyClassInit(tree, *dc, itc->second, diag);
    assert(!dc->children.empty() && dc->children[0]
        && "classifyClassInit left no default field tuple");
    e.children.push_back(std::move(dc->children[0]));

    e.class_op_chain = true;
    e.op_collapse_head = collapsed_head;   // the ROLE, decided here; desugar obeys it
    e.op_bin_eid = bin_id;
    e.op_aug_eid = aug_id;
    e.op_eq_lhs_eid = eq_lhs_id;
    e.op_eq_rhs_eid = eq_rhs_id;
    e.op_move_eid = move_id;
    e.inferred_type = itc->second.type;
    e.op_type = itc->second.type;
    return true;
}

// An ARITY-1 UNARY on a class (`-a`) is a PRODUCER, like a class binary: it yields a whole
// class value that needs a home. So it is stamped, never lowered here — desugar answers the
// "raw storage or live object?" question with the whole statement in hand, and a unary at a
// chain's HEAD collapses into the accumulator exactly as a head construction does.
// The result class is the OPERAND's, and nothing else. children become [operand, default
// field-init tuple]; op_un_eid is the only operator a unary runs (it writes the whole value,
// so there is no seed and nothing to fuse).
bool stampClassUnary(parse::Tree& tree, parse::Node& e, std::string const& op,
                     widen::TypeRef oref, diagnostic::Sink& diag) {
    if (e.class_op_chain) return true;    // inferExpr may revisit a node; stamp once
    widen::TypeRef C = widen::strip(oref);
    if (widen::form(C) != widen::Type::Form::kSlid) return false;
    auto itc = tree.classes.find(C);
    if (itc == tree.classes.end()) return false;
    parse::Node& operand = *e.children[0];

    // The arity-1 `op<OP>(Self^)`. findClassOperator ranks the [_$recv, one param] shapes,
    // so a class carrying BOTH a 2-arg (binary) and a 1-arg (unary) `op-` picks the unary.
    int un_id = findClassOperator(tree, C, operand, "op" + op, diag);
    if (un_id < 0) return false;          // -2 (ambiguous, already reported) reads as absent
    // The move INTO a live target, resolved HERE so desugar can CALL it by name — a transfer
    // synthesized after classify reaches the class's move operator no other way.
    parse::Node self_val;
    self_val.kind = parse::Kind::kIdentExpr;
    self_val.inferred_type = itc->second.type;
    self_val.file_id = e.file_id;
    self_val.tok = e.tok;
    int move_id = findClassOperator(tree, C, self_val, "op<--", diag);
    if (move_id < 0) move_id = -1;

    // The accumulator's construction value: the class's DEFAULT field-init tuple, built by
    // the ONE construction funnel, so a chain accumulator is born like any other local.
    auto dc = std::make_unique<parse::Node>();
    dc->kind = parse::Kind::kVarDeclStmt;
    dc->return_type = itc->second.type;
    dc->file_id = e.file_id;
    dc->tok = e.tok;
    classifyClassInit(tree, *dc, itc->second, diag);
    assert(!dc->children.empty() && dc->children[0]
        && "classifyClassInit left no default field tuple");
    e.children.push_back(std::move(dc->children[0]));

    e.class_op_chain = true;
    e.op_un_eid = un_id;
    e.op_move_eid = move_id;
    e.inferred_type = itc->second.type;
    e.op_type = itc->second.type;
    return true;
}

// Lower an AGGREGATE-target conversion `(AggType = src)` BY SLOT — the same way tuples
// are handled everywhere else. `((Class,int) = rhs)` becomes the per-slot tuple
// `((Class = rhs[0]), (int = rhs[1]))`: each slot is itself a conversion, re-inferred
// here, so a class slot reuses the class path (construct + op=), a primitive slot the
// value grid (checkConvertCompat), and a nested aggregate recurses. The node BECOMES a
// kTupleExpr typed as the target (an array target rides the array<->tuple bridge at its
// binding). Cross-form falls out — `rhs[i]` is form-agnostic (a tuple OR array source),
// and the target's form dictates the slot types. Source access, evaluated ONCE per slot:
// a tuple LITERAL yields its element i in place; a bare lvalue is re-indexed (no side
// effect to duplicate); a non-bare source is spilled to `_$cinit` first (evaluated once)
// and the spill spliced into `prelude`.
void lowerAggregateConversion(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    widen::TypeRef T = e.return_type;
    widen::TypeRef Ts = widen::strip(T);
    parse::Node& operand = *e.children[0];
    widen::TypeRef srcT = widen::strip(operand.inferred_type);
    e.inferred_type = T;
    bool srcIsLit = operand.kind == parse::Kind::kTupleExpr;
    // The source must be an aggregate (an array IS a homogeneous tuple) of matching arity.
    if (!srcIsLit && !isAggregateType(srcT)) {
        diagnostic::report(diag, {e.file_id, e.tok,
            "Cannot convert '" + widen::spellOrEmpty(operand.inferred_type)
            + "' to '" + widen::spellOrEmpty(T) + "'.", {}});
        return;
    }
    int n = aggregateSlotCount(Ts);
    int sn = srcIsLit ? static_cast<int>(operand.children.size())
                      : aggregateSlotCount(srcT);
    if (sn != n) {
        diagnostic::report(diag, {e.file_id, e.tok,
            "Cannot convert '" + widen::spellOrEmpty(operand.inferred_type)
            + "' to '" + widen::spellOrEmpty(T) + "'; slot count differs ("
            + std::to_string(sn) + " vs " + std::to_string(n) + ").", {}});
        return;
    }
    int f = e.file_id, tk = e.tok;
    // Capture the per-slot target types BEFORE the loop — aggregateSlotType may intern an
    // array element type, dangling a held get()-ref (arena safety).
    std::vector<widen::TypeRef> slotTypes;
    for (int i = 0; i < n; i++) slotTypes.push_back(aggregateSlotType(Ts, i));
    // Source-once (THE SHARED PREDICATE + THE SPILL FUNNEL): a tuple LITERAL yields its
    // element i in place; a RE-READABLE lvalue (`isReReadableLvalue` — an access path with
    // no side effect, the SAME question the destructure and the slot-wise explode ask) is
    // re-indexed in place; anything else (a call / op, OR an lvalue whose subscript or base
    // carries a side effect, e.g. `arr[pick()]` / `get()^.f`) is SPILLED via `spillToTemp`
    // to a `_$cinit` temp evaluated ONCE, and each slot indexes the temp. (The old gate was
    // the SHALLOW `isBareLvalue`, which sees only the outermost node kind — so a side-effecting
    // subscript/base was re-cloned per slot and the side effect ran N times; todo.txt.) The
    // spill decl is stashed for desugar to hoist before the statement (agg_conv_spill).
    widen::TypeRef opType = operand.inferred_type;
    std::unique_ptr<parse::Node> spill_decl;
    int spill_id = -1;
    if (!srcIsLit) {
        std::vector<std::unique_ptr<parse::Node>> spills;
        if (spillIfNotReReadable(tree, e.children[0], "_$cinit", spills)) {
            spill_decl = std::move(spills[0]);
            spill_id = e.children[0]->resolved_entry_id;   // e.children[0] is now the temp read
        }
    }
    std::vector<std::unique_ptr<parse::Node>> lit;
    if (srcIsLit) for (auto& c : operand.children) lit.push_back(std::move(c));
    std::vector<std::unique_ptr<parse::Node>> slots;
    for (int i = 0; i < n; i++) {
        std::unique_ptr<parse::Node> src_i;
        if (srcIsLit) {
            src_i = std::move(lit[i]);
        } else {
            // `src[i]` — index the bare lvalue, or the `_$cinit` spill temp.
            auto idx = std::make_unique<parse::Node>();
            idx->kind = parse::Kind::kIntLiteral;
            idx->text = std::to_string(i);
            idx->file_id = f;
            idx->tok = tk;
            auto base = std::make_unique<parse::Node>();
            if (spill_id >= 0) {
                base->kind = parse::Kind::kIdentExpr;
                base->name = "_$cinit";
                base->resolved_entry_id = spill_id;
                base->inferred_type = opType;
                base->file_id = f;
                base->tok = tk;
            } else {
                base = cloneExpr(operand);
            }
            auto ix = std::make_unique<parse::Node>();
            ix->kind = parse::Kind::kIndexExpr;
            ix->file_id = f;
            ix->tok = tk;
            ix->children.push_back(std::move(base));
            ix->children.push_back(std::move(idx));
            src_i = std::move(ix);
        }
        auto conv = std::make_unique<parse::Node>();
        conv->kind = parse::Kind::kConvertExpr;
        conv->return_type = slotTypes[i];
        conv->file_id = f;
        conv->tok = tk;
        conv->children.push_back(std::move(src_i));
        inferExpr(tree, *conv, widen::kNoType, diag);   // recurse: class / primitive / nested
        slots.push_back(std::move(conv));
    }
    // Build the per-slot tuple.
    auto tuple = std::make_unique<parse::Node>();
    tuple->kind = parse::Kind::kTupleExpr;
    tuple->file_id = f;
    tuple->tok = tk;
    tuple->inferred_type = T;
    for (auto& s : slots) tuple->children.push_back(std::move(s));
    e.class_conversion = false;
    e.children.clear();
    if (spill_decl) {
        // Keep e a kConvertExpr carrying [spill decl, tuple]; desugar hoists the spill.
        e.agg_conv_spill = true;
        e.children.push_back(std::move(spill_decl));
        e.children.push_back(std::move(tuple));
        e.inferred_type = T;
    } else {
        // No spill — e simply BECOMES the tuple.
        e.kind = parse::Kind::kTupleExpr;
        for (auto& c : tuple->children) e.children.push_back(std::move(c));
        e.inferred_type = T;
    }
}

void classifyStmt(parse::Tree& tree, parse::Node& s,
                  widen::TypeRef fn_return_type, diagnostic::Sink& diag,
                  std::vector<std::unique_ptr<parse::Node>>* prelude = nullptr) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            // A GLOBAL initializes to a CONSTANT EXPRESSION, whatever its type. This is
            // the FIRST thing asked of a global declaration — before any type-specific
            // arm below can rewrite the rhs (a construction becomes its arg tuple, a
            // class init becomes a field tuple), and so before the class arm can return
            // early, which is how a class global used to miss this rule entirely. It
            // used to be asked only of a PRIMITIVE-typed global, on the reasoning that a
            // compound one is built by a synthesized ctor rather than folded — so every
            // class / array / tuple global skipped it, and `global Class b = a;` compiled
            // (and then filled b from a before running b's ctor ON TOP of the copy — the
            // wrong answer that made the hole visible). Rejecting the whole shape closes
            // that by construction: a global class can only come from a construction or a
            // constant aggregate, both of which BUILD IN PLACE, so no copy exists to
            // mis-order.
            if (s.is_global && !s.children.empty() && s.children[0]
                && !isConstantInit(*s.children[0])) {
                diagnostic::report(diag, {s.file_id, s.name_tok,
                    "Initializer for '" + s.name
                    + "' is not a constant expression.", {}});
                return;
            }
            // DECL-INIT swap (`Class a <--> rhs`): default-construct `a` (spliced as a
            // prelude), then rewrite THIS statement into an existing-var swap `a <--> rhs`
            // — which the kSwapStmt handler dispatches to a user op<--> or the default
            // field swap. Net (canon "weird but allowed"): a takes rhs's value, rhs resets
            // to the fresh default. Only a class carries a swap operator; a non-class
            // swap-init is rejected. Needs a prelude to splice the default-construct into.
            if (s.default_swap_init && prelude && !s.children.empty() && s.children[0]) {
                // An INFERRED swap-init (`nw <--> ex`, typeless target) carries no declared
                // type yet — infer it from the source (as the copy/move inferred path does)
                // BEFORE the class-type check, so a class source is recognized rather than
                // mis-rejected. A typed swap-init (`Class nw <--> ex`) already has its type.
                if (s.return_type == widen::kNoType && s.resolved_entry_id >= 0
                    && !s.is_const) {
                    inferExpr(tree, *s.children[0], widen::kNoType, diag);
                    parse::Node& rhs = *s.children[0];
                    widen::TypeRef inferred =
                        (isLiteralKind(rhs.kind) && rhs.strong_type != widen::kNoType)
                            ? rhs.strong_type
                            : rhs.inferred_type;
                    s.return_type = inferred;
                    tree.entries[s.resolved_entry_id].slids_type = inferred;
                    tree.entries[s.resolved_entry_id].alias_label = rhs.alias_label;
                }
                auto itc = tree.classes.find(widen::strip(s.return_type));
                if (widen::form(widen::strip(s.return_type)) != widen::Type::Form::kSlid
                    || itc == tree.classes.end()) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "Swap-initialization requires a class type.", {}});
                    return;
                }
                std::string aname = s.name;
                int aid = s.resolved_entry_id;
                widen::TypeRef atype = s.return_type;
                auto dc = std::make_unique<parse::Node>();
                dc->kind = parse::Kind::kVarDeclStmt;
                dc->name = aname;
                dc->name_tok = s.name_tok;
                dc->resolved_entry_id = aid;
                dc->return_type = atype;
                dc->file_id = s.file_id;
                dc->tok = s.tok;
                classifyClassInit(tree, *dc, itc->second, diag);
                prelude->push_back(std::move(dc));
                auto a_ident = std::make_unique<parse::Node>();
                a_ident->kind = parse::Kind::kIdentExpr;
                a_ident->name = aname;
                a_ident->name_tok = s.name_tok;
                a_ident->resolved_entry_id = aid;
                a_ident->inferred_type = atype;
                a_ident->file_id = s.file_id;
                a_ident->tok = s.tok;
                auto rhs = std::move(s.children[0]);
                s.kind = parse::Kind::kSwapStmt;
                s.name.clear();
                s.resolved_entry_id = -1;
                s.default_swap_init = false;
                s.children.clear();
                s.children.push_back(std::move(a_ident));
                s.children.push_back(std::move(rhs));
                classifyStmt(tree, s, fn_return_type, diag, prelude);
                return;
            }
            // A class-typed local is constructed: normalize the init (slot /
            // default / zero) into a typed construction tuple. The pointer /
            // array / strong-const init rules below don't apply.
            if (widen::form(widen::strip(s.return_type)) == widen::Type::Form::kSlid) {
                auto it = tree.classes.find(widen::strip(s.return_type));
                if (it != tree.classes.end()) {
                    // DECL-INIT operator dispatch for a NON-tuple source. A class var
                    // DECLARED with a bare `= expr` (copy) or `<-- expr` (move) whose source
                    // is a scalar / class value routes through the ONE binding funnel
                    // (dispatchAssignInit): when a user op= / op<-- matches it default-
                    // constructs the fresh var (needs_construct) and rewrites THIS statement
                    // into `name.op=(rhs)` (canon "construct THEN op"). A TUPLE source is NOT
                    // dispatched here — `Class c = (1,2,3)` is the class-from-VALUE funnel
                    // (buildClassFromValue below): a tuple op= yields a conversion the transfer
                    // peel builds in place, exactly as a tuple SLOT / array ELEMENT does, so the
                    // op= decision lives in ONE place for every position. EXCLUDED here: a tuple
                    // source, no matching operator, or no prelude.
                    if (prelude && !s.children.empty() && s.children[0]
                        && s.children[0]->kind != parse::Kind::kTupleExpr) {
                        // Type the source for the operator probe. A CONSTRUCTION's type is
                        // its class — read it from the resolved target; do NOT inferExpr a
                        // construction here (that mis-processes it; the BUILD block below
                        // owns constructions). Any other source is inferred normally.
                        if (s.children[0]->kind == parse::Kind::kCallExpr
                            && s.children[0]->is_construction) {
                            if (s.children[0]->resolved_entry_id >= 0)
                                s.children[0]->inferred_type =
                                    tree.entries[s.children[0]->resolved_entry_id]
                                        .slids_type;
                        } else {
                            // A class-PRODUCING operator expr (binary `a op b` / arity-1
                            // unary `op a`) needs the DECL class as context to pick its
                            // result operator — inferExpr then lowers it to a class-rvalue
                            // temp (kConvertExpr[class_conversion]) that the elide path
                            // below builds in place. Other sources infer context-free.
                            bool op_expr =
                                s.children[0]->kind == parse::Kind::kBinaryExpr
                             || s.children[0]->kind == parse::Kind::kUnaryExpr;
                            inferExpr(tree, *s.children[0],
                                      op_expr ? s.return_type : widen::kNoType, diag);
                        }
                        std::string opname = s.default_move_init ? "op<--" : "op=";
                        if (dispatchAssignInit(tree, s, s.return_type, *s.children[0],
                                               opname, /*expr_lhs=*/false, diag, prelude,
                                               /*needs_construct=*/true))
                            return;
                    }
                    // `Class y = Class(args)` is a BUILD, identical to `Class y(args)`
                    // / `Class y = (args)` — NOT a same-type whole-object copy. Replace
                    // the construction rhs with a tuple of its RAW ctor args (still
                    // un-inferred here) so it field-inits in place through the normal
                    // path below. Left as a construction kCallExpr, it would infer to
                    // the class type and hit the same-type-copy early-return, leaving a
                    // class-typed kCallExpr that codegen cannot emit (segfault).
                    if (!s.children.empty() && s.children[0]
                        && s.children[0]->kind == parse::Kind::kCallExpr
                        && s.children[0]->is_construction) {
                        auto ctor = std::move(s.children[0]);
                        auto tup = std::make_unique<parse::Node>();
                        tup->kind = parse::Kind::kTupleExpr;
                        tup->file_id = ctor->file_id;
                        tup->tok = ctor->tok;
                        for (auto& a : ctor->children)
                            if (a) tup->children.push_back(std::move(a));
                        s.children[0] = std::move(tup);
                        // `= Class(args)` is a BUILD, identical to `Class c(args)` — its
                        // arg tuple is a FIELD LIST, never a value op= (a `(1)` collapsing
                        // to the scalar `1` must not match op=(int)). Mark it so the routing
                        // below field-lists it rather than probing the class-from-VALUE funnel.
                        s.construction_init = true;
                    }
                    // An rvalue AGGREGATE source (a call / op result) can't be indexed in
                    // place. Spill it — evaluated once — so the per-field spread in
                    // classifyClassInit sees an lvalue and handles it EXACTLY like an
                    // array/tuple variable (partial fill, recursion into class-typed fields,
                    // per-slot conversion). A bare variable is free to re-index in place, but
                    // an index / deref / call / op source can carry a side effect (`g[bump()]`)
                    // the spread would otherwise re-run once per field.
                    // SEQ placement (THE SPILL FUNNEL): the temp is read only by THIS decl's
                    // rhs, so it is parked on the node and desugar hoists it into the
                    // statement's kSeqExpr — it dies at the SEMICOLON. It used to go into the
                    // `prelude`, which made it an ordinary local of the enclosing BLOCK and
                    // kept a whole aggregate (classes and all) alive to the end of the scope.
                    std::unique_ptr<parse::Node> cinit_decl;
                    if (prelude && !s.children.empty() && s.children[0]
                        && s.children[0]->kind != parse::Kind::kTupleExpr) {
                        inferExpr(tree, *s.children[0], widen::kNoType, diag);
                        // An AGGREGATE source spread across the fields is taken apart by the ONE
                        // helper: a re-readable lvalue is spread in place; a call / op /
                        // side-effecting access path is evaluated ONCE into `_$cinit` (SEQ-placed
                        // below via seqSpill). A scalar source is not a spread — left alone.
                        widen::TypeRef srcT = s.children[0]->inferred_type;
                        if (isAggregateType(widen::strip(srcT))) {
                            std::vector<std::unique_ptr<parse::Node>> spills;
                            if (spillIfNotReReadable(tree, s.children[0], "_$cinit", spills))
                                cinit_decl = std::move(spills[0]);
                        }
                    }
                    // A `= (tuple)` value-init routes through the class-from-VALUE funnel: a
                    // matching op= yields a `(Class = tuple)` conversion the transfer peel
                    // below builds in place (`Class c = (1,2,3)` -> default-construct + op=,
                    // no temp), and no match falls back to field-list construction. The `(args)`
                    // CONSTRUCTION form (construction_init) and every non-tuple source
                    // field-list / spread via classifyClassInit directly.
                    if (!s.construction_init && !s.children.empty() && s.children[0]
                        && s.children[0]->kind == parse::Kind::kTupleExpr) {
                        s.children[0] = buildClassFromValue(tree, it->second,
                            std::move(s.children[0]), s.file_id, s.tok, diag);
                    } else {
                        // A non-tuple source (a scalar, a same-class value) does not route through
                        // buildClassFromValue, but a VALUE-INIT one (`Bw bw = 5`) still carries
                        // value-init context to its fields — so a nested class field whose op=
                        // would need to dispatch is diagnosed, exactly as a tuple source's is. A
                        // `(args)` CONSTRUCTION (construction_init) does not.
                        classifyClassInit(tree, s, it->second, diag, /*subobject=*/false,
                                          /*value_init=*/!s.construction_init);
                    }
                    // The init is now this class's FIELD TUPLE (or an op= conversion). A field
                    // taking a class LVALUE is a COPY INTO a field that does not exist yet —
                    // peel it off (`Holder h( c )` becomes `Holder h; h.c_ = c;`), and an op=
                    // conversion slot is peeled the same way, so the object is constructed
                    // before it is copied / op'd into.
                    applyTransferSplit(tree, s, fn_return_type, diag, prelude);
                    // Park the spill on the rhs LAST, so the passes above see the field tuple.
                    if (cinit_decl && !s.children.empty() && s.children[0]) {
                        s.children[0] = seqSpill(std::move(cinit_decl),
                                                 std::move(s.children[0]));
                    }
                    return;
                }
            }
            // A no-initializer array/tuple whose leaves are classes is default-
            // constructed in place: synthesize the per-element/-slot construction
            // value (classZeroValue recurses arrays + tuples and default-constructs
            // each class leaf with its field defaults), so codegen field-inits it
            // exactly like a written initializer. The scalar-class case returned
            // above; a pointer leaf or pure-primitive aggregate is left
            // uninitialized (hasInPlaceClass is false), as before.
            if (s.children.empty() && widen::hasInPlaceClass(s.return_type)) {
                auto init = classZeroValue(tree, s.return_type, s.file_id, s.tok, diag);
                init->inferred_type = s.return_type;
                s.children.push_back(std::move(init));
                return;
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
                // INFERRED decl-init (`x = e`): the type is now known — if it is a class
                // with a matching user op= / op<--, route through the SAME binding funnel as
                // a typed decl-init (default-construct THEN operator; the funnel spills a
                // class RVALUE source). The source was inferred just above.
                if (prelude && !s.is_const && !s.is_global && !s.children.empty()
                    && s.children[0]
                    && s.children[0]->kind != parse::Kind::kTupleExpr
                    && widen::form(widen::strip(s.return_type))
                           == widen::Type::Form::kSlid) {
                    std::string opname = s.default_move_init ? "op<--" : "op=";
                    if (dispatchAssignInit(tree, s, s.return_type, *s.children[0], opname,
                                           /*expr_lhs=*/false, diag, prelude,
                                           /*needs_construct=*/true))
                        return;
                }
                // A TYPELESS CONST that constfold could NOT fold (its entry
                // slids_type is still unset) is resolved HERE: infer the rhs type;
                // an AGGREGATE / pointer is a not-mutable VARIABLE (flip the entry
                // to kLocalVar + deepConst, like a typed const variable — desugar's
                // is_const-from-entry-kind then makes codegen allocate it); a
                // leftover SCALAR is the genuine "not a constant expression".
                if (s.is_const && s.return_type == widen::kNoType
                    && s.resolved_entry_id >= 0
                    && tree.entries[s.resolved_entry_id].slids_type == widen::kNoType) {
                    parse::Node& rhs = *s.children[0];
                    widen::TypeRef inferred =
                        (isLiteralKind(rhs.kind) && rhs.strong_type != widen::kNoType)
                            ? rhs.strong_type : rhs.inferred_type;
                    if (inferred != widen::kNoType
                        && widen::form(widen::strip(inferred))
                               != widen::Type::Form::kPrimitive) {
                        widen::TypeRef ct = widen::deepConst(inferred);
                        s.return_type = ct;
                        parse::Entry& e = tree.entries[s.resolved_entry_id];
                        e.kind = parse::EntryKind::kLocalVar;
                        e.slids_type = ct;
                        e.alias_label = rhs.alias_label;
                    } else if (inferred != widen::kNoType) {
                        diagnostic::report(diag, {s.file_id, s.name_tok,
                            "Initializer for '" + s.name
                            + "' is not a constant expression.", {}});
                    }
                }
                // (The global constant-initializer rule is asked at the TOP of this arm,
                // for EVERY type — see isConstantInit.)
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
                // A class-BEARING aggregate (a tuple / array of classes) never reaches the
                // kSlid funnel above — findClassOperator needs a class — so its copies are
                // peeled off HERE: a whole-value copy of the aggregate, and, PER SLOT, any
                // element that is a class lvalue. A CONSTRUCTION element still builds in
                // its slot; only a copy is deferred until the slot exists.
                applyTransferSplit(tree, s, fn_return_type, diag, prelude);
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
            // NO pre-inference interception here. The rhs is inferred FIRST, bottom-up:
            // `a = b + c` evaluates `b + c` on its own terms (dispatching on the LHS
            // OPERAND's class — see tryLowerClassBinary) into a temp, and only THEN is the
            // temp assigned into `a` via op=. That ordering IS operator precedence: `+`
            // binds tighter than `=`, so the assignment target must never get a say in how
            // the binary is evaluated. The old target-keyed chain fuse (tryLowerBinaryChain)
            // ran before this loop precisely because it had no operand types to work with —
            // so it keyed on `a`, re-associated `b + c + d` into `a.op+(b,c); a.op+=(d)`,
            // and clobbered `a` mid-expression (visible to any later operand that reads it).
            // It is deleted. Eliding the temp back into `a` is an OPTIMIZATION and belongs
            // in desugar, where the operand types exist (see todo.txt: Stage 3 proper).
            for (auto& ch : s.children) {
                if (ch) inferExpr(tree, *ch, lref, diag);
            }
            // A class lvalue with a matching user `op=` dispatches to it (canon: the
            // synthesized default copy applies only when NO matching operator exists).
            // Rewrite `a = rhs` to `a.op=(rhs)` and resolve through the method-call path.
            if (!s.children.empty() && s.children[0]
                && dispatchAssignInit(tree, s, lref, *s.children[0], "op=",
                                      /*expr_lhs=*/false, diag, prelude,
                                      /*needs_construct=*/false)) {
                return;
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

            // A class lvalue with a matching user compound operator (`op+=`, …)
            // dispatches to it: `a += b` -> `a.op+=(b)`. The bare-name form maps to
            // rewriteAssignToOperatorCall (receiver from s.name); the complex-lvalue
            // form already holds children [lvalue, rhs]. With no matching operator we
            // fall through to the numeric path below, which rejects arithmetic on a
            // class as before.
            if (widen::form(widen::strip(s.return_type)) == widen::Type::Form::kSlid) {
                inferExpr(tree, rhs, widen::kNoType, diag);
                std::string opname = "op" + op + "=";
                bool expr_lhs = s.children.size() == 2;
                if (dispatchAssignInit(tree, s, s.return_type, rhs, opname,
                                       expr_lhs, diag,
                                       prelude, /*needs_construct=*/false)) {
                    return;
                }
                // ONE-LEVEL COERCION, the compound-assign leg. This family probes through
                // findClassOperator, not pickOverload, so it needs the retry spelled here:
                // `s += x` where no `op+=` takes x but `op=` can BUILD one from it becomes
                // `s.op+=( (C = x) )`. Without it, `a + x` coerced and `a += x` did not,
                // which is the same operator disagreeing with itself over the spelling.
                std::size_t before = diag.records.size();
                std::size_t ri = expr_lhs ? 1 : 0;
                if (diag.records.size() == before
                    && coerceOperandToClass(tree, s.return_type, *s.children[ri], diag)
                    && dispatchAssignInit(tree, s, s.return_type, *s.children[ri], opname,
                                          expr_lhs, diag, prelude,
                                          /*needs_construct=*/false)) {
                    return;
                }
                // NO viable compound operator on a class — reject CLEANLY. The numeric path
                // below would SILENTLY accept it: commonType SUCCEEDS for two identical class
                // types, so `a += b` on a class with no op+= emitted a struct `add` — INVALID
                // IR (the compiler must never produce that). The kBinaryExpr arm has had this
                // guard since the class-binary work; the aug-assign arm never got it, and no
                // test could see the hole because a class with no operator is not something
                // the operator tests exercise.
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Operator '" + op + "=' is not defined on class '"
                    + widen::spellOrEmpty(s.return_type) + "'.", {}});
                return;
            }

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
                if (isAggregateType(s.return_type)) {
                    // Slot-wise `lhs <<= cnt` — TAKEN APART like every other aggregate
                    // operation (the shared block below owns it; a shift's count is NOT
                    // flexed to the leaf type, so it says so).
                    explodeAggregateAug(tree, s, fn_return_type, diag, /*is_shift=*/true);
                    return;
                }
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
            // arith / bitwise. Infer rhs WITHOUT context first so a tuple literal
            // keeps its tuple shape — the aggregate case must match the binary arm,
            // which infers its operands context-free.
            inferExpr(tree, rhs, widen::kNoType, diag);
            // Aggregate lvalue op aggregate rhs (array/tuple): the SAME element-
            // wise path the binary arm uses, so `lhs op= rhs` can't diverge from
            // `lhs op rhs`. A mixed array/tuple result is a tuple; the store back
            // into the lvalue is gated by the aggregate assignment relation, so a
            // per-element narrow is caught here, not in codegen.
            if (isAggregateType(s.return_type)) {
                explodeAggregateAug(tree, s, fn_return_type, diag, /*is_shift=*/false);
                return;
            }
            // Scalar: re-infer rhs with the lvalue type as context so a literal
            // flexes to the lvalue's width, then commonType drives the op.
            inferExpr(tree, rhs, s.return_type, diag);
            widen::TypeRef optyRef;
            if (!widen::commonType(s.return_type, rhs.inferred_type, optyRef)) {
                // The arithmetic convenience's aug twins (+= -= *= /= %=): an
                // INTEGER rhs converts to a FLOAT lvalue's type. The reverse
                // (int lvalue, float rhs) stays rejected — the store-back
                // would narrow.
                optyRef = arithFloatMix(s.return_type, rhs.inferred_type, op);
                if (optyRef == widen::kNoType
                    || widen::strip(optyRef) != widen::strip(s.return_type)) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "No common type for '" + spellForMessage(s.return_type) + "' and '"
                        + spellForMessage(rhs.inferred_type)
                        + "'; use an explicit type conversion.", {}});
                    return;
                }
                flexIntLiteralToFloat(rhs, optyRef);
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
            // A class store target with a matching user `op=` dispatches to it — the same
            // rule kAssignStmt applies to a bare-name target, now for a complex lvalue
            // (`obj.f = x` / `p^ = x` / `arr[i] = x`). Mirrors kMoveStmt / kSwapStmt.
            if (dispatchAssignInit(tree, s, lvalue.inferred_type, *s.children[1], "op=",
                                   /*expr_lhs=*/true, diag, prelude,
                                   /*needs_construct=*/false)) {
                return;
            }
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
            // A class lhs with a matching user `op<--` dispatches to it (else the
            // default field move applies). Children are already [receiver, arg].
            if (dispatchAssignInit(tree, s, lhs.inferred_type, *s.children[1], "op<--",
                                   /*expr_lhs=*/true, diag, prelude,
                                   /*needs_construct=*/false)) {
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
            // A class operand with a matching user `op<-->` dispatches to it (else the
            // default field swap applies). Children are already [receiver, arg].
            if (dispatchAssignInit(tree, s, a.inferred_type, b, "op<-->",
                                   /*expr_lhs=*/true, diag, prelude,
                                   /*needs_construct=*/false)) {
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
            // Every destructure — copy `=`, move `<--`, swap `<-->` — desugars to per-slot
            // statements against the source, so each slot binds through the ordinary
            // assignment path (dispatching a user op= / op<-- / op<-->). By slot,
            // recursively. Needs a prelude to splice the per-slot statements ahead.
            if (prelude) {
                desugarDestructure(tree, s, fn_return_type, diag, *prelude);
                return;
            }
            // No prelude (defensive): fall back to whole-value type-check only. children[0]
            // = rhs (a TUPLE or ARRAY), [1..] = declarator / nested slots.
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
            // delete <ptr>; — the operand must be a pointer (reference / iterator);
            // any pointer expression is allowed (lvalue nulled back, rvalue freed).
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
            // dtor runs). resolve resolved it; type it and require a class. It must
            // ALSO be reached through a pointer dereference (`ptr^.~()`): that is
            // the placement-new cleanup pattern, where the compiler does NOT manage
            // the object's lifetime. A directly-named / auto-managed object is
            // destroyed at scope exit, so an explicit `.~()` on it would double-
            // destruct — reject it.
            parse::Node& recv = *s.children[0];
            inferExpr(tree, recv, widen::kNoType, diag);
            widen::TypeRef rs = widen::strip(recv.inferred_type);
            bool dtor_is_class = widen::form(rs) == widen::Type::Form::kSlid
                              && tree.classes.count(rs) > 0;
            bool dtor_is_prim = widen::form(rs) == widen::Type::Form::kPrimitive;
            if (recv.inferred_type != widen::kNoType
                && !dtor_is_class && !dtor_is_prim) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A destructor call '.~()' requires a class object; got '"
                    + spellForMessage(recv.inferred_type) + "'.", {}});
            } else if (recv.inferred_type != widen::kNoType
                       && recv.kind != parse::Kind::kDerefExpr) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A destructor call '.~()' is only allowed on a "
                    "placement-constructed object reached through a pointer "
                    "('ptr^.~()'); an automatically-managed object is destroyed "
                    "at scope exit.", {}});
            } else if (dtor_is_prim) {
                // A PRIMITIVE pointee has no destructor: the call is a NO-OP —
                // the pseudo-destructor rule generic code needs (a container's
                // destroy path spells `ptr^.~()` for EVERY element type; the
                // primitive flavors must compile). Neutralize the statement.
                s.kind = parse::Kind::kBlockStmt;
                s.children.clear();
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
            // Statement-form `Class(args);` — a nameless scope-lifetime object.
            // Build its construction tuple + type it as the class; desugar lowers
            // it to a synthetic kVarDeclStmt (dtor at end of the enclosing scope).
            if (s.is_construction) {
                classifyConstruction(tree, s, diag);
                // A nameless STATEMENT-form object's only observable effect is its
                // ctor/dtor side effects. A class with NEITHER (no explicit and no
                // synthesized hooks — a trivial class) makes the statement a no-op,
                // which is a compile error (spec: nameless.sl form 1). ctor/dtor are
                // language-paired, so a single need implies both.
                if (!diagnostic::hasErrors(diag)) {
                    widen::TypeRef ct = widen::strip(s.inferred_type);
                    auto ci = tree.classes.find(ct);
                    if (ci != tree.classes.end()
                        && !ci->second.needs_ctor && !ci->second.needs_dtor) {
                        diagnostic::report(diag, {s.file_id, s.tok,
                            "A nameless class statement has no effect: '"
                            + ci->second.name
                            + "' has no constructor or destructor.", {}});
                    }
                }
                return;
            }
            // Pick the overload (if any) and infer each arg with the chosen
            // param type as context.
            classifyCall(tree, s, diag);
            return;
        }
        case parse::Kind::kMethodCallStmt: {
            inferMethodCall(tree, s, diag);
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
            // THE RETURN SLOT IS STORAGE, AND A CLASS CAN ONLY BE COPIED INTO. A tuple/array
            // LITERAL with a class TRANSFER in a slot (`return (a, b);`) must order EACH SLOT
            // — constructed, then copied into, while a CONSTRUCTION slot still builds in place
            // — and that is exactly what the declarator funnel does. It only needs a NAME to
            // address, which an sret slot has not got. So give it one: bind the literal to a
            // `_$ret` local and return that. applyTransferSplit then peels the slots, and
            // desugar's NRVO builds `_$ret` DIRECTLY in the caller's slot — so the name costs
            // nothing. (If NRVO ever declines, the return is a bare lvalue and the codegen
            // path below still constructs the slot before transferring: correct, one temp.)
            // A SINGLE class return from a VALUE — `return (7,8)` / `return 5` where the return
            // type is a class the value op='s / field-lists / converts — is the return-slot twin
            // of a decl: it must bind to `_$ret` too, so the declarator funnel (buildClassFromValue
            // / dispatchAssignInit: op= / field-list / convert, in place) runs and NRVO builds
            // `_$ret` in the caller's slot. Without this the generic return path below rejects the
            // value->class assignment. EXCLUDED (the arm below already builds / transfers them
            // directly): a CONSTRUCTION (`return P(1,2)` — builds in the slot) and a value already
            // of the class type (an lvalue copy, a class-producing chain).
            bool agg_transfer_ret = !s.children.empty() && s.children[0]
                && s.children[0]->kind == parse::Kind::kTupleExpr
                && hasClassTransferSlot(*s.children[0], fn_return_type);
            bool single_class_ret = false;
            if (!agg_transfer_ret && !s.children.empty() && s.children[0]
                && widen::form(widen::strip(fn_return_type)) == widen::Type::Form::kSlid
                && tree.classes.count(widen::strip(fn_return_type)) > 0) {
                parse::Node& v = *s.children[0];
                bool is_ctor = v.kind == parse::Kind::kCallExpr && v.is_construction;
                if (!is_ctor) {
                    inferExpr(tree, v, fn_return_type, diag);
                    single_class_ret = widen::deepStrip(v.inferred_type)
                                    != widen::deepStrip(fn_return_type);
                }
            }
            if (agg_transfer_ret || single_class_ret) {
                Spill sp = spillToTemp(tree, std::move(s.children[0]), fn_return_type,
                                       "_$ret");
                // The aggregate-transfer case moves lvalue slots in (canon case 3: `ret^ <-- a`);
                // a single-class VALUE return is a plain value-init (op= / field-list), not a
                // move of an existing object, so it takes an ordinary copy-init.
                if (agg_transfer_ret) sp.decl->default_move_init = true;
                std::vector<std::unique_ptr<parse::Node>> body;
                classifyStmt(tree, *sp.decl, fn_return_type, diag, &body);
                body.push_back(std::move(sp.decl));
                auto ret = std::make_unique<parse::Node>();
                ret->kind = parse::Kind::kReturnStmt;
                ret->file_id = s.file_id;
                ret->tok = s.tok;
                ret->children.push_back(std::move(sp.read));
                // Classify it like any other return — `_$ret` is now a bare LVALUE source, so
                // it takes the defaults the arm parks below. NRVO usually claims it and none
                // of that is emitted; but NRVO can DECLINE (another returned-local is live
                // here), and then the slot is constructed and transferred into exactly as any
                // other lvalue return is. Minting the node and skipping the arm left the
                // declined path with no defaults — and so with the original bug.
                classifyStmt(tree, *ret, fn_return_type, diag, &body);
                body.push_back(std::move(ret));
                s.kind = parse::Kind::kBlockStmt;
                s.children.clear();
                for (auto& b : body) s.children.push_back(std::move(b));
                return;
            }
            for (auto& ch : s.children) {
                if (ch) {
                    inferExpr(tree, *ch, fn_return_type, diag);
                    checkValueAssign(tree, fn_return_type, *ch, diag);
                }
            }
            // A whole-value TRANSFER into the slot (a class-bearing LVALUE source) has the
            // same rule and no slots to split: codegen constructs the slot and then transfers
            // into it. It needs the class's DEFAULTS to construct, and those come from
            // ClassInfo, which is ours — park them on the return (children[1]). An RVALUE
            // source BUILDS the slot (the elide) and needs nothing.
            if (!s.children.empty() && s.children[0]
                && widen::hasInPlaceClass(widen::strip(fn_return_type))
                && isBareLvalue(*s.children[0])) {
                auto def = classZeroValue(tree, fn_return_type, s.file_id, s.tok, diag);
                def->inferred_type = fn_return_type;
                s.children.push_back(std::move(def));
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
            classifyScope(tree, s, diag);
            return;
        case parse::Kind::kEnumDecl:
            // Enum members were lowered to kConst entries at resolve and folded
            // by constfold; the enum node carries nothing to type-infer.
            return;
        case parse::Kind::kBlockStmt:
            // A nested scope: type-infer each contained statement (splicing any
            // prelude a statement emits, e.g. a class-init spill temp).
            classifyStmtList(tree, s.children, fn_return_type, diag);
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
                std::size_t count = at.dims.front();   // capture before internArray dangles at
                widen::TypeRef e = (at.dims.size() <= 1) ? at.elem
                    : widen::internArray(at.elem,
                        std::vector<int>(at.dims.begin() + 1, at.dims.end()));
                slots.assign(count, e);
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
        case parse::Kind::kGlobalScopeStmt:
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
                std::size_t nlabel = clause.children.size() - 1;   // body = back()
                for (std::size_t j = 0; j < nlabel; j++) {
                  if (clause.children[j]) {
                    parse::Node& label = *clause.children[j];
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
                }
                classifyStmt(tree, *clause.children.back(), fn_return_type, diag);
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
            // A block-scope TEMPLATE stays pristine — instantiation types its clones.
            if (!s.type_params.empty()) return;
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
            // A block-scope CLASS TEMPLATE is a pristine pattern — skip it.
            if (!s.type_params.empty()) return;
            // A local class is a scope like any other: type its member bodies
            // (self-bound; field refs are already kFieldExpr from resolve), nested
            // scopes recursing through the same routine. Construction lowers at use.
            classifyScope(tree, s, diag);
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
    // A switch is a return-terminator iff it has a default, no clause has a break
    // escaping it (a break leaves via the enclosing loop, not by returning), and
    // every clause's exit reaches a return. With no implicit fall-through a clause
    // exits the switch at its body's `}` — so a non-returning clause is acceptable
    // only if it has a trailing `continue` into a LATER clause (the fall-through
    // chain must end in a return); a non-returning last clause, or a non-returning
    // clause with no continue, escapes the switch without returning.
    if (s.kind == parse::Kind::kSwitchStmt) {
        bool has_default = false;
        for (std::size_t i = 1; i < s.children.size(); i++) {
            parse::Node const& clause = *s.children[i];
            std::size_t nlabel = clause.children.size() - 1;
            for (std::size_t j = 0; j < nlabel; j++)
                if (!clause.children[j]) has_default = true;
            if (containsBreak(*clause.children.back())) {
                return false;   // an escaping break leaves without returning
            }
        }
        if (!has_default) return false;
        for (std::size_t i = 1; i < s.children.size(); i++) {
            parse::Node const& clause = *s.children[i];
            if (endsInReturnNode(*clause.children.back())) continue;
            bool has_cont = (clause.text == "continue");
            bool is_last = (i + 1 == s.children.size());
            if (!has_cont || is_last) return false;
        }
        return true;
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
    // A METHOD carries the implicit receiver `_$recv` at params[0]. Free functions
    // are validated in resolve (Pass 1a); a method skips that pass, so run the same
    // two signature checks here for it — without them a typeless-no-default param
    // reaches codegen as kNoType and aborts (bug), and a required-after-optional
    // param is silently accepted.
    bool is_method = !fn.params.empty() && fn.params[0]
                  && fn.params[0]->name == "_$recv";
    if (is_method) {
        bool seen_default = false;
        for (auto& p : fn.params) {
            if (!p) continue;
            bool has_default = !p->children.empty() && p->children[0];
            if (p->return_type == widen::kNoType && !has_default) {
                diagnostic::report(diag, {p->file_id, p->name_tok,
                    "Parameter '" + p->name
                        + "' needs an explicit type or a default value.", {}});
            }
            if (has_default) seen_default = true;
            else if (seen_default) {
                diagnostic::report(diag, {p->file_id, p->name_tok,
                    "A required parameter cannot follow an optional parameter.", {}});
            }
        }
    }
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
    // num_required = the count of LEADING params with no default (a method's
    // `_$recv` at [0] has none, so it counts as required). Free functions also have
    // this set in resolve; re-setting it here is idempotent and covers METHODS,
    // whose entries resolve never stamped it.
    int nreq = 0;
    for (auto& p : fn.params) {
        if (p && !p->children.empty() && p->children[0]) break;
        nreq++;
    }
    e.num_required = nreq;
    // Two DEFINED methods with the same name + identical (now-final) param types are a
    // duplicate DEFINITION, not an overload — functions catch this in resolve, but the
    // method overload-set registration allows the same name, so detect it here.
    // Reported on the later one (lower id = earlier). Both must be DEFINITIONS: a
    // forward decl + a definition is a redeclaration, not a duplicate.
    if (is_method && e.defined && !e.is_template && e.tmpl_args.empty()) {
        for (int id = 0; id < (int)tree.entries.size(); id++) {
            if (id >= fn.resolved_entry_id) break;
            parse::Entry const& q = tree.entries[id];
            // A template and its instances share the name (and, for a T-free
            // parameter list, the very signature) BY DESIGN — never a duplicate.
            if (q.is_template || !q.tmpl_args.empty()) continue;
            if (q.kind == parse::EntryKind::kFunction && q.defined
                && q.owner_ns_frame == e.owner_ns_frame
                && q.name == e.name && q.param_types == e.param_types) {
                diagnostic::report(diag, {fn.file_id, fn.name_tok,
                    "Duplicate definition of '" + fn.name + "'.",
                    {{q.def_file_id, q.def_tok, "first defined here"}}});
                break;
            }
        }
    }
}

// THE SLOT-WISE EXPLODE, STATEMENT FORM: an aug-assign on an AGGREGATE lvalue becomes
// `t[i] op= u[i]` per slot, in a block. Per-slot STATEMENTS rather than one exploded
// expression, because the operator the author wrote is the COMPOUND one — a class slot
// must reach its own `op+=`, and rebuilding it out of `op+` would be a different program
// (and impossible for a class that has only `op+=`). Each per-slot statement then goes
// through classifyStmt exactly as if it had been written by hand, so a class slot
// dispatches, a numeric slot widens, and a NESTED aggregate slot explodes again.
//
// The rhs is an aggregate of matching shape, or a SCALAR that BROADCASTS (`arr += 1`).
// It is prepared by the same trichotomy the expression form uses (literal taken apart /
// re-readable lvalue indexed / else spilled once) — GROUP placement here, since the temp
// is read by sibling STATEMENTS: it and they share one block and it dies at the semicolon.
void explodeAggregateAug(parse::Tree& tree, parse::Node& s, widen::TypeRef fn_return_type,
                         diagnostic::Sink& diag, bool is_shift) {
    parse::Node& rhs = *s.children.back();
    std::string const op = s.text;
    // A shift's COUNT is not a slot value — it stands alone (flexing it to the lhs leaf
    // type would mis-type it). Any other scalar rhs flexes, so `int8[3] += 1` keeps int8.
    if (!is_shift && !isAggregateType(rhs.inferred_type)) {
        inferExpr(tree, rhs, aggregateLeafType(s.return_type), diag);
    }
    int nslots = aggregateSlotCount(widen::strip(s.return_type));
    // The ONE shape rule (as in the expression form): an aggregate rhs must match the
    // lvalue's slot count; a scalar rhs broadcasts. Everything else — leaf types, narrowing,
    // bitwise-on-a-float, a non-integer shift count — is asked of each SLOT by the ordinary
    // arms, since a slot is now an ordinary operation.
    if (isAggregateType(rhs.inferred_type)
        && aggregateSlotCount(widen::strip(rhs.inferred_type)) != nslots) {
        diagnostic::report(diag, {s.file_id, s.tok,
            "Aggregate shapes differ: '" + widen::spellOrEmpty(s.return_type) + "' vs '"
            + widen::spellOrEmpty(rhs.inferred_type) + "'.", {}});
        s.inferred_type = s.return_type;
        s.op_type = s.return_type;
        return;
    }
    // The lvalue as a PATH: the complex form already carries one; the bare-name form gets
    // an ident naming the same entry.
    std::unique_ptr<parse::Node> lvpath;
    if (s.children.size() == 2) {
        lvpath = cloneExpr(*s.children[0]);
    } else {
        lvpath = std::make_unique<parse::Node>();
        lvpath->kind = parse::Kind::kIdentExpr;
        lvpath->name = s.name;
        lvpath->name_tok = s.name_tok;
        lvpath->resolved_entry_id = s.resolved_entry_id;
        lvpath->inferred_type = s.return_type;
        lvpath->file_id = s.file_id;
        lvpath->tok = s.tok;
    }
    std::vector<std::unique_ptr<parse::Node>> spills;
    std::unique_ptr<parse::Node> rhsNode = std::move(s.children.back());
    AggOperand ra = prepareOperand(tree, rhsNode, nslots, spills);

    std::vector<std::unique_ptr<parse::Node>> body;
    for (auto& sp : spills) body.push_back(std::move(sp));
    for (int i = 0; i < nslots; i++) {
        widen::TypeRef slotT = aggregateSlotType(widen::strip(s.return_type), i);
        auto st = std::make_unique<parse::Node>();
        st->kind = parse::Kind::kAugAssignStmt;
        st->text = op;
        st->file_id = s.file_id;
        st->tok = s.tok;
        st->children.push_back(makeSlotPath(*lvpath, (std::size_t)i, slotT));
        st->children.push_back(operandSlot(ra, (std::size_t)i));
        classifyStmt(tree, *st, fn_return_type, diag, &body);
        body.push_back(std::move(st));
    }
    s.kind = parse::Kind::kBlockStmt;
    s.text.clear();
    s.name.clear();
    s.resolved_entry_id = -1;
    s.inferred_type = widen::kNoType;
    s.op_type = widen::kNoType;
    s.children.clear();
    for (auto& b : body) s.children.push_back(std::move(b));
}

// Recursively complete every MEMBER function's signature (typeless-param infer +
// default capture + num_required) across a scope tree — methods, namespace free
// functions, and nested scopes — BEFORE any body is typed, so a forward call (even
// across scopes) sees the full signature. Members only; a function nested in a BODY
// is handled by classifyFunctionBody's own pre-pass. ctor/dtor hooks (no entry) are
// no-op'd by classifyFunctionSignature's resolved_entry_id guard.
// Infer typeless FIELD types from their (constfold-folded) default literals and
// patch the class's kSlid layout slots. Mirrors classifyFunctionSignature for params,
// but a field is a LAYOUT slot, so after inferring we re-intern the handle with the
// updated slots — same name+def_id key, so every reference sees the patch. An inferred
// field is always PRIMITIVE (a const-expr default can't be a class), so the resolve
// needs-ctor/dtor fixpoint, which saw a kNoType slot and contributed nothing, stays
// correct — no re-run needed.
void classifyClassSignature(parse::Tree& tree, widen::TypeRef classType,
                            diagnostic::Sink& diag) {
    widen::TypeRef s = widen::strip(classType);
    auto it = tree.classes.find(s);
    if (it == tree.classes.end()) return;
    parse::ClassInfo& info = it->second;
    bool changed = false;
    for (std::size_t i = 0;
         i < info.field_types.size() && i < info.field_params.size(); i++) {
        if (info.field_types[i] != widen::kNoType) continue;   // explicitly typed
        // A kNoType slot means resolve DEFERRED this field — which it does only for a
        // typeless field that HAS a default (a no-default typeless field errored at
        // resolve and was never registered). So a default must be present; a missing
        // one is a compiler-invariant break, not a case to silently skip.
        parse::Node* p = info.field_params[i];
        assert(p && !p->children.empty() && p->children[0]
            && "classifyClassSignature: a deferred field must carry a default");
        parse::Node& def = *p->children[0];
        inferExpr(tree, def, widen::kNoType, diag);
        if (!isLiteralKind(def.kind)) {
            diagnostic::report(diag, {def.file_id, def.tok,
                "A field default must be a constant expression.", {}});
            continue;
        }
        info.field_types[i] = def.inferred_type;   // preferred no-width handle
        p->return_type = def.inferred_type;
        changed = true;
    }
    if (changed) {
        std::string name = widen::get(s).name;     // capture BEFORE internSlid (arena)
        int def_id = widen::get(s).def_id;
        widen::internSlid(name, info.field_types, def_id);
    }
}

// THE OVERLOAD-SET DECLARATION CHECK — a default parameter makes a candidate's arity a
// RANGE (num_required .. param count), so two overloads of one name can both admit the
// SAME arg count. When they also agree on the parameter types up to that count, no call
// at that arity can ever distinguish them, and the set is rejected WHERE IT IS DECLARED:
//
//     void fn(int a);            // range 1..1
//     void fn(int a, int b = 0); // range 1..2  -- both admit 1 arg, prefix (int) identical
//
// Either fn(i) is ambiguous (so fn(int) can never be called) or it picks fn(int) (so b's
// default can never be used). Both readings are broken, so neither is chosen — it is an
// error to WRITE the pair. Runs after classifyScopeSignatures, the point where every
// param type (including one INFERRED from its default) and num_required are final, so it
// is order-independent: it compares finished entries, not parse nodes.
// Two entries with IDENTICAL param types are skipped — that is a forward decl + its
// definition (methods keep separate entries), owned by the duplicate-definition check.
// With no default anywhere, an overlapping arity means identical full signatures, so this
// check can only fire on a default — and it covers METHODS by construction (the shared
// entry model; a method's `_$recv` sits in both prefixes and cancels out).
void checkOverloadDefaultCollisions(parse::Tree& tree, diagnostic::Sink& diag) {
    // A method's owner frame is a CLASS frame; its receiver param is held out of the
    // spelled signature and the reported arg count, exactly as at a call site.
    std::map<int, bool> class_frames;
    for (parse::Entry const& e : tree.entries) {
        if (e.kind == parse::EntryKind::kClass && e.ns_frame_id >= 0)
            class_frames[e.ns_frame_id] = true;
    }
    for (std::size_t j = 0; j < tree.entries.size(); j++) {
        parse::Entry const& later = tree.entries[j];
        if (later.kind != parse::EntryKind::kFunction) continue;
        if (later.is_template || !later.tmpl_args.empty()) continue;   // patterns /
        for (std::size_t i = 0; i < j; i++) {                          // instances
            parse::Entry const& first = tree.entries[i];
            if (first.kind != parse::EntryKind::kFunction) continue;
            if (first.is_template || !first.tmpl_args.empty()) continue;
            if (first.name != later.name) continue;
            if (first.owner_ns_frame != later.owner_ns_frame) continue;
            if (first.parent_frame_id != later.parent_frame_id) continue;
            if (first.param_types == later.param_types) continue;   // decl + def
            std::size_t lo = (std::size_t)(first.num_required > later.num_required
                                           ? first.num_required : later.num_required);
            std::size_t hi = first.param_types.size() < later.param_types.size()
                           ? first.param_types.size() : later.param_types.size();
            for (std::size_t n = lo; n <= hi; n++) {
                bool same = true;
                for (std::size_t k = 0; k < n && same; k++) {
                    if (first.param_types[k] != later.param_types[k]) same = false;
                }
                if (!same) continue;
                int recv = class_frames.count(later.owner_ns_frame) ? 1 : 0;
                std::size_t nargs = n >= (std::size_t)recv ? n - (std::size_t)recv : 0;
                reportAmbiguity(tree, later.file_id, later.tok,
                    "Ambiguous overloads of '" + later.name + "': a call with "
                        + std::to_string(nargs) + " argument"
                        + (nargs == 1 ? "" : "s") + " matches both.",
                    {(int)i}, recv, diag);
                break;
            }
        }
    }
}

void classifyScopeSignatures(parse::Tree& tree, parse::Node& node,
                             diagnostic::Sink& diag) {
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kFunctionDef
         || m->kind == parse::Kind::kFunctionDecl) {
            // A TEMPLATE's signature is a pattern (resolved at registration); its
            // instances run this per-instance at instantiation.
            if (!m->type_params.empty()) continue;
            classifyFunctionSignature(tree, *m, diag);
        } else if (m->kind == parse::Kind::kNamespaceDecl
                || m->kind == parse::Kind::kClassDef) {
            // A CLASS TEMPLATE is a pristine pattern — no signature to classify;
            // its instances run these per-instance at instantiation.
            if (m->kind == parse::Kind::kClassDef && !m->type_params.empty())
                continue;
            // Infer this class's typeless field types BEFORE any body / construction
            // is typed (this pre-pass runs ahead of classifyScope).
            if (m->kind == parse::Kind::kClassDef)
                classifyClassSignature(tree, m->return_type, diag);
            classifyScopeSignatures(tree, *m, diag);
        }
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
        if (ch && ch->kind == parse::Kind::kFunctionDef
            && ch->type_params.empty()) {   // a template's signature is a pattern
            classifyFunctionSignature(tree, *ch, diag);
        }
    }
    classifyStmtList(tree, fn.children, fn.return_type, diag);
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

}  // namespace

// --instantiate demands left by resolve: the FUNCTION / METHOD template
// flavors, minted here where those instances are born. A bare `f<...>` is a
// file-scope template; `Owner:member<...>` reaches one level into a class or
// namespace. Args arrive canonical (the writer canonicalized them), so they
// bind by direct spelling-intern.
void classifyInstantiationDemands(parse::Tree& tree, diagnostic::Sink& diag) {
    for (auto& d : tree.inst_demands) {
        if (d.consumed) continue;
        d.consumed = true;
        widen::TypeRef t = widen::intern(d.spelling);
        if (widen::form(t) != widen::Type::Form::kTmplUse) continue;  // reported
        std::string name = widen::get(t).name;
        // ARITY-ONLY OVERLOADING: a demand spelling names template + type
        // args but carries NO arity, so it addresses EVERY same-name sibling
        // whose type-list fits — instantiate them all (an arity the consumer
        // never called emits as unused external code: bloat, not breakage).
        std::vector<int> tids;
        auto colon = name.find(':');
        if (colon == std::string::npos) {
            for (std::size_t i = 0; i < tree.entries.size(); i++) {
                parse::Entry const& e = tree.entries[i];
                if (e.kind == parse::EntryKind::kFunction && e.is_template
                    && e.tmpl_args.empty()
                    && e.owner_ns_frame < 0 && e.name == name) {
                    tids.push_back((int)i);
                }
            }
        } else {
            std::string owner = name.substr(0, colon);
            std::string member = name.substr(colon + 1);
            int frame = -1;
            for (std::size_t i = 0; i < tree.entries.size(); i++) {
                parse::Entry const& e = tree.entries[i];
                if ((e.kind == parse::EntryKind::kClass
                     || e.kind == parse::EntryKind::kNamespace)
                    && e.owner_ns_frame < 0 && e.name == owner
                    && e.ns_frame_id >= 0) {
                    frame = e.ns_frame_id;
                    break;
                }
            }
            for (std::size_t i = 0; frame >= 0 && i < tree.entries.size(); i++) {
                parse::Entry const& e = tree.entries[i];
                if (e.kind == parse::EntryKind::kFunction && e.is_template
                    && e.tmpl_args.empty()
                    && e.owner_ns_frame == frame && e.name == member) {
                    tids.push_back((int)i);
                }
            }
        }
        if (tids.empty()) {
            diagnostic::report(diag, {-1, -1,
                "Unknown template in instantiation demand '" + d.spelling
                + "'.", {}});
            continue;
        }
        std::vector<widen::TypeRef> slots = widen::get(t).slots;
        bool any_fit = false;
        for (int tid : tids) {
            auto ti = tree.templates.find(tid);
            if (ti == tree.templates.end()) continue;
            if (slots.size() != ti->second.def->type_params.size()) continue;
            any_fit = true;
            std::vector<widen::TypeRef> bound;
            for (widen::TypeRef s : slots)
                bound.push_back(widen::removeConst(widen::deepStrip(s)));
            parse::Node stub;
            stub.kind = parse::Kind::kCallExpr;
            stub.name = d.spelling;
            stub.file_id = -1;
            stub.tok = -1;
            instantiateAndClassify(tree, tid, bound, stub, diag);
        }
        if (!any_fit) {
            diagnostic::report(diag, {-1, -1,
                "Wrong number of template arguments in instantiation demand '"
                + d.spelling + "'.", {}});
        }
    }
}

void run(parse::Tree& tree, diagnostic::Sink& diag) {
    parse::Node* program = nullptr;
    for (auto& n : tree.nodes) {
        if (n && n->kind == parse::Kind::kProgram) {
            program = n.get();
            break;
        }
    }
    if (!program) return;

    // Signature pre-pass first: a call to a later-defined function (or method) must
    // see its completed param types + captured defaults. Recurse ALL scopes so a
    // method / namespace-member signature is complete before ANY body is typed.
    classifyScopeSignatures(tree, *program, diag);

    // Signatures are final — reject any overload set a default parameter has made
    // indistinguishable at some arity, at the DECLARATION rather than at a call.
    checkOverloadDefaultCollisions(tree, diag);

    // The program is itself a scope (the implicit global namespace): type its
    // member bodies — top-level function bodies, const inits, and every nested
    // namespace/class — through the one uniform routine.
    classifyScope(tree, *program, diag);

    // --instantiate demands whose flavor mints at classify (functions and
    // methods) — before the final drains, so anything they mint joins them.
    classifyInstantiationDemands(tree, diag);

    // Any straggler class-template instances (a resolution re-entry outside the
    // instantiateAndClassify path) get their late stages before the splice.
    classifyFreshClassInstances(tree, diag);

    // Splice the template instances minted during the walk into their host lists,
    // each right AFTER its template's definition node. Deferred to here because a
    // mid-walk splice would invalidate the walker's iterators; placed at the def
    // (not the list end) so an instance never lands after a `return`, which would
    // break trailing-return analysis downstream.
    for (auto& pending : tree.pending_tmpl_instances) {
        if (!pending.host_list || !pending.node) continue;
        auto& list = *pending.host_list;
        std::size_t at = 0;   // def-not-found fallback: the FRONT (never the end —
                              // a statement after a `return` breaks analysis)
        for (std::size_t i = 0; i < list.size(); i++) {
            if (list[i].get() == pending.after) { at = i + 1; break; }
        }
        list.insert(list.begin() + at, std::move(pending.node));
    }
    tree.pending_tmpl_instances.clear();
}

}  // namespace classify
