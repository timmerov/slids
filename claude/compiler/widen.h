#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace diagnostic { struct Sink; }

namespace widen {

// kChar is its own category (not kUnsignedInt): canon (expression/widen.sl,
// function/overload_fn.sl) — char widens to integer types, but integer types
// never widen or match char; only char matches char. The distinction is
// DIRECTIONAL, so it must live in the kind itself. For grading/conversion char
// behaves as an 8-bit unsigned SOURCE (zext, same-sign-with-unsigned rungs).
enum class Category { kBool, kChar, kSignedInt, kUnsignedInt, kFloat };

struct TypeKind {
    Category cat;
    int bits;
};

// ---------------------------------------------------------------------------
// Structured types (replacing the type-as-string debt). A TypeRef is a stable
// handle into a process-lifetime interned arena; equal types share a handle, so
// type-equality is a handle compare and no consumer ever re-parses a spelling.
// The human-readable spelling is produced by spell() only at diagnostic / ##type
// boundaries.  Migration is staged: while the AST still carries strings, callers
// bridge via intern()/spell(); the round-trip is exact (spell(intern(s)) == s).
// ---------------------------------------------------------------------------

using TypeRef = int;
constexpr TypeRef kNoType = -1;

// def_id sentinel marking a kSlid as a TEMPLATE TYPE PARAMETER pattern leaf (`T` in
// `T add<T>(T a, T b)` — name = the parameter, no such class exists). Only ever found
// in a template entry's pattern param/return types; unification binds it, and nothing
// downstream of classify ever sees one (instances carry concrete types).
constexpr int kTmplParamDefId = -2;

struct Type {
    enum class Form {
        kNone,        // the kNoType sentinel — "no type" (distinct from void); a
                      // no-type field reads as this, so form()==kVoid never matches it
        kPrimitive,   // bool/char/intN/uintN/floatN + the no-width int/uint/float + intptr
        kVoid,        // void
        kAnyptr,      // anyptr — the typeless null pointer
        kPointer,     // T^  (reference)
        kIterator,    // T[] (iterator)
        kArray,       // T[d0][d1]... — elem + dims in source order
        kSlid,        // a named slid/class type (not a known primitive)
        kTuple,       // (T0, T1, ...) — slots (lands with tuples)
        kAlias,       // a transparent named type — name + underlying; sees through
                      // to underlying for all structural queries, spells as name
        kTmplUse,     // an UNRESOLVED template-alias use `Name<arg, ...>` — name +
                      // args in `slots`. Minted by intern from the parse-side
                      // spelling; resolve's resolveTypeRef expands it (substitute
                      // the alias template's pattern) into a transparent kAlias,
                      // so one never survives resolve. Unknown/sizeless until then.
        kConst,       // a const-qualified type — wraps `underlying`. Transparent for
                      // every structural query / matching (strip/deepStrip/classify
                      // see through it; no enforcement yet), but VISIBLE in spell()
                      // (`const T` / `(const T)^`). Binds loosest: an OUTER kConst is
                      // deep (`const T^` = const pointer to const data); const on a
                      // pointee (`(const T)^`) is the shallow form.
    };
    Form form = Form::kNone;
    Category cat = Category::kSignedInt;   // kPrimitive
    int bits = 0;                          // kPrimitive
    std::string name;                      // kPrimitive spelling tag / kSlid / kAlias name
    TypeRef pointee = kNoType;             // kPointer / kIterator
    TypeRef elem = kNoType;                // kArray element
    TypeRef underlying = kNoType;          // kAlias — the type it transparently names
    std::vector<int> dims;                 // kArray dims, source order
    std::vector<TypeRef> slots;            // kTuple / kSlid (field types)
    int def_id = -1;                       // kSlid: scope disambiguator — the
                                           // defining frame id for a LOCAL class,
                                           // else -1 (file-scope). Part of identity
                                           // (structKey), so two same-named local
                                           // classes are distinct handles. The NAME
                                           // stays bare; the unique LLVM symbol is
                                           // minted from this at codegen, never stored.
    bool has_ctor = false;                 // kSlid: an explicit `_()` -> a
    bool has_dtor = false;                 //   <Name>__$ctor/__$dtor symbol exists
    bool needs_ctor = false;               // kSlid: TRANSITIVE — explicit, or a
    bool needs_dtor = false;               //   field whose class needs it (run hooks)
    // kSlid: is the hook's BODY (`@<Name>__$ctor__impl`) written in THIS TU? has_ctor
    // says the class HAS a ctor — a per-CLASS fact every TU agrees on, since it comes
    // from the header. This says who DEFINES it, which differs per TU: a header class's
    // `_(){}` may live in any ONE `.sl` (a hook is a method with restrictions, and a
    // declared member is definable anywhere), so the TU emitting the COMPLETE method
    // must `declare` the impl it calls whenever the body is somebody else's. A hook is
    // not a frame entry, so this cannot be asked of the symbol table the way a method's
    // decl/def split is — it is answered where every opening is visible and recorded here.
    bool ctor_here = false;
    bool dtor_here = false;
    // kSlid: how THIS TU emits the class's symbols. Set by resolve from the file the
    // class is DECLARED in; read by codegen.
    //   kInternal — declared in a `.sl`: the class is private to this TU, so EVERY
    //               symbol it owns is `internal`. Nothing outside can name the class, so
    //               nothing outside can call them — and two unrelated `.sl` files may
    //               each declare a class of the same name without colliding at link.
    //   kDefine   — declared in a `.slh` whose SIBLING is this TU: emit the synthesized
    //               symbols with EXTERNAL linkage. This is the only TU that does.
    //   kDeclare  — declared in a `.slh`, this TU is not the sibling: emit no synthesized
    //               definitions at all, only `declare`s to link against the sibling's.
    enum class Linkage { kInternal, kDefine, kDeclare };
    Linkage linkage = Linkage::kInternal;
    // kSlid: the class is DECLARED incomplete (a trailing `...` in its header) — it has
    // PRIVATE fields hidden from an importer, whose layout/size the importer cannot know.
    // So its `__$sizeof` is an EXTERNAL function (the completer defines it, importers call
    // it), an importer sizes an instance at runtime + constructs via `__$ctor`, and never
    // sees the layout. True in EVERY TU that sees the incomplete header (the completer AND
    // importers), even though the completer holds the full layout.
    bool opaque = false;
    // kSlid: the class DERIVES from an opaque class. Its base sub-object sits at slot 0
    // with an unknown size, so its OWN field offsets are not compile-time constants in an
    // importer — they come from the completer's exported `__$offsets` table. Implies
    // `opaque` (the size is a runtime call too). True in EVERY TU that sees the header,
    // exactly like `opaque`, so both halves agree on the layout rule.
    bool runtime_layout = false;
    // (COMPUTED LAYOUT is not a stored flag: whether a class/tuple/array embeds a
    // dynamically-sized value is a pure STRUCTURAL fact of universal per-class facts
    // — the opaque flag and the slot lists — so it is asked, not published. See
    // sizeIsDynamic / slidComputedLayout below.)
};

// The alignment an OPAQUE class is laid out at. Its real alignment belongs to the
// completer, so a layout that must place one without asking over-aligns to the
// platform maximum — the same value every opaque `alloca` already uses. Making this
// a fixed convention is what keeps the offset of the FIRST non-static field a
// compile-time constant (and is why no `__$alignof` export is needed).
constexpr int kOpaqueAlign = 16;

// Intern a slids type spelling, returning a stable handle. Round-trips exactly:
// spell(intern(s)) == s for every well-formed spelling; intern(s) is stable.
TypeRef intern(std::string const& spelling);

// Intern a spelling, mapping the empty spelling ("no type yet") to kNoType.
// The boundary helper for the string-stage / TypeRef-field edges.
TypeRef internOrNone(std::string const& spelling);

// Construct + intern a tuple type from its slot handles (dedups with a parsed
// `(...)` spelling; a 1-slot tuple collapses to that slot).
TypeRef internTuple(std::vector<TypeRef> const& slots);

// Structural constructors — build + dedup a composite type from child HANDLES,
// not a spelling. These are how resolve rebuilds a type after resolving its leaves
// (an alias-bearing composite can't be reconstructed from its spelling). The arena
// dedups STRUCTURALLY (form + children), so internPointer(x) and intern("<x>^")
// agree, and a kAlias-leaf composite is distinct from a kSlid-leaf one.
TypeRef internPointer(TypeRef pointee);
TypeRef internIterator(TypeRef pointee);
TypeRef internArray(TypeRef elem, std::vector<int> const& dims);

// A transparent alias type: spells as `name`, but sees through to `underlying`
// for every structural query (cat/bits/llvm/size/known/predicates). Minted only
// by resolve (which has the symbol table that knows name -> underlying).
TypeRef internAlias(std::string const& name, TypeRef underlying);

// A const-qualified type wrapping `underlying`. Transparent for matching
// (strip/deepStrip/classify see through it), VISIBLE in spell(). Collapses
// const(const(T)) to const(T); const(kNoType) is kNoType. Placement encodes
// deep vs shallow: internConst(internPointer(T)) = `const T^` (deep — the
// pointer and the data it reaches are const); internPointer(internConst(T)) =
// `(const T)^` (shallow — a mutable pointer to const data). The param-munge
// builds the shallow form for a non-`mutable` reference parameter.
TypeRef internConst(TypeRef underlying);

// A named class/slid type carrying its field-slot types. `def_id` is the scope
// disambiguator (a LOCAL class's defining frame id; -1 for file-scope) — part of
// identity, so two same-named local classes get distinct handles while the name
// stays bare. resolve calls this once the field list is known to attach the
// layout. Codegen reads get(ref).slots for the struct definition.
TypeRef internSlid(std::string const& name, std::vector<TypeRef> const& slots,
                   int def_id = -1);

// Mark a class's EXPLICIT lifecycle hooks (a `_(){}` / `~(){}` definition, so
// the ctor/dtor symbols exist). Seeds needs_ctor/needs_dtor = has_*. Takes the
// class handle (from internSlid) so it sets the right def-id'd type.
void setSlidLifecycle(TypeRef ref, bool has_ctor, bool has_dtor);

// Set a class's TRANSITIVE needs (explicit OR a field-class needs it), computed
// by resolve's fixpoint after all classes are registered. Takes the class handle.
void setSlidNeeds(TypeRef ref, bool needs_ctor, bool needs_dtor);

// Set how this TU emits the class's symbols (see Type::Linkage). Resolve decides it
// from the file the class is DECLARED in; codegen reads it back via slidLinkage.
void setSlidLinkage(TypeRef ref, Type::Linkage linkage);
Type::Linkage slidLinkage(TypeRef ref);

// Set/read whether the class is DECLARED incomplete (opaque to importers; see
// Type::opaque). Resolve sets it from a header's trailing `...`; codegen reads it.
void setSlidOpaque(TypeRef ref, bool opaque);
bool slidOpaque(TypeRef ref);

// Set/read whether the class derives from an opaque base (see Type::runtime_layout).
// Resolve propagates it down base chains once every class is registered; codegen reads
// it to emit / consult the `__$offsets` table instead of a struct GEP.
void setSlidRuntimeLayout(TypeRef ref, bool runtime_layout);
bool slidRuntimeLayout(TypeRef ref);

// Does this type's STORAGE embed an opaque class by value — directly, or through
// any depth of class fields / array elements / tuple slots? Equivalently: is its
// byte size not a compile-time constant of this stage. An opaque class answers
// true in EVERY TU (the flag is universal), so every TU classifies a layout
// identically — that universality is what lets two TUs agree on a COMPUTED layout
// without either exporting it. A pointer / iterator breaks the walk.
bool sizeIsDynamic(TypeRef ref);

// A COMPUTED-layout class: its field list is visible everywhere it is nameable,
// but a field's size lives in another module — so no TU can fold the layout, and
// every TU instead lays it out by ONE CONVENTION: fields at their natural offsets,
// any dynamically-sized field at kOpaqueAlign with its size ROUNDED UP to
// kOpaqueAlign (the completer-exported `__$size16`). All the arithmetic besides
// the leaf sizes is compile-time, so every offset and the total size reduce to
// `constant + Σ mult·size16(leaf)` — see layoutSlotOffset / layoutSizeExpr.
// (An OPAQUE class is not computed — its layout is the completer's private,
// llc-folded struct; computed is the visible-field complement.)
bool slidComputedLayout(TypeRef ref);

// The byte size / alignment of a STATICALLY-sized type, INCLUDING a class (laid
// out like the literal struct codegen lowers it to: natural alignment, padding,
// an empty class is 1 byte). -1 when the size is dynamic (sizeIsDynamic) or the
// form has no size. Unlike typeByteSize — which answers -1 for EVERY class so
// sizeof(Class) stays a runtime value llc owns — these are the numeric layout
// the computed-layout convention positions a static field with.
long long staticByteSize(TypeRef ref);
long long staticByteAlign(TypeRef ref);

// A layout value under the computed-layout convention: a compile-time constant
// plus multiples of opaque leaf classes' rounded sizes (`__$size16` symbols —
// codegen materializes each term). Every term's value is a multiple of
// kOpaqueAlign, which is why the constant part absorbs ALL alignment rounding.
struct LayoutExpr {
    long long cst = 0;
    std::vector<std::pair<TypeRef, long long>> terms;   // (opaque class, multiplier)
};

// The byte offset of slot `k` of a computed-layout class/tuple `agg` (stripped).
LayoutExpr layoutSlotOffset(TypeRef agg, std::size_t k);

// The byte size of any type under the convention. `round16` rounds the result up
// to kOpaqueAlign (the embedding stride/size); a computed class/tuple is already
// 16-rounded by construction. Static types yield a pure constant.
LayoutExpr layoutSizeExpr(TypeRef ref, bool round16);

// Set whether each hook's BODY is written in this TU (see Type::ctor_here). Resolve
// decides it in registerClassBody, where every OPENING of the class is visible — the
// declaration and the definition may sit in different ones. Codegen reads it back to
// choose between calling an impl it defines and declaring one it does not.
void setSlidHookHere(TypeRef ref, bool ctor_here, bool dtor_here);
bool slidCtorHere(TypeRef ref);
bool slidDtorHere(TypeRef ref);

// The disambiguated LLVM symbol base for a class kSlid handle (file-scope = bare
// name; local = name + ".<frame>"). The ONE place a class name is mangled, and
// only into emitted symbols — desugar (ctor/dtor defs) and codegen (calls / sizeof
// / struct) both call it so definition and use agree.
std::string classSymbol(TypeRef ref);

// Remove every const qualifier (at all levels), rebuilding the wrappers — the
// `<mutable>` cast's type transform. Unlike deepStrip it PRESERVES aliases and
// pointer/iterator/array/tuple structure; only kConst facets are dropped.
TypeRef removeConst(TypeRef ref);

// Const-qualify every MUTABLE position of a type — the mirror of removeConst, used
// to make a `const`-declared aggregate / pointer a not-mutable VARIABLE. An array
// const-qualifies its element (`(const int)[3]`), a tuple each slot, a pointer /
// iterator BOTH the pointer itself and its pointee (deep: `const (const int)^`), a
// primitive / class the leaf. So not-mutability survives index / slot / deref.
TypeRef deepConst(TypeRef ref);

// Replace every template TYPE-PARAMETER marker leaf (a kSlid with def_id ==
// kTmplParamDefId) whose name appears in `names` with the corresponding handle
// from `args`, rebuilding composite wrappers. An alias wrapper whose underlying
// changes drops its (now-lying) label. The one substitution walk behind
// alias-template expansion.
TypeRef substituteTypeParams(TypeRef ref, std::vector<std::string> const& names,
                             std::vector<TypeRef> const& args);

// Peel any alias layers, returning the first non-alias handle (the underlying
// structure). Predicates that switch on form() use this to see through aliases.
TypeRef strip(TypeRef ref);

// Recursively remove EVERY alias (at all levels), rebuilding wrappers — so two
// types that are equal modulo aliases (`Integer^` and `IntPtr = int^`) deep-strip
// to the SAME handle. Use for type-equality across alias boundaries (ptr casts,
// same-pointee checks).
TypeRef deepStrip(TypeRef ref);

// Recursively remove EVERY alias, PRESERVING const (deepStrip erases both).
// The template binder canonicalizes T bindings through this (canon 2026-08-18):
// a binding's BURIED const is the write layer and must survive into T
// (`same<(const int)^>` returns without a const drop), while aliases still
// canonicalize (`add<Integer>` IS `add<int>`).
TypeRef deepAliasStrip(TypeRef ref);

// spell(), but kNoType renders as the empty string (the inverse of internOrNone).
std::string spellOrEmpty(TypeRef ref);

// Render a handle back to its slids spelling.
std::string spell(TypeRef ref);

// Structured access to an interned type.
Type const& get(TypeRef ref);
inline Type::Form form(TypeRef ref) { return get(ref).form; }

// Structured-type readers — the TypeRef-native cores. The std::string overloads
// above bridge via intern() during the migration (Stage 1); consumers move onto
// these as they migrate. classify(string) stays the primitive-name lexer that
// feeds intern(), so it is NOT a bridge wrapper.
bool classify(TypeRef ref, TypeKind& out);
bool isKnownType(TypeRef ref);
long long typeByteSize(TypeRef ref);

// True iff the type is a class, or an array/tuple (recursively) that reaches a
// class WITHOUT crossing a pointer/iterator — a value whose class leaves are
// stored in place and so are default-constructed at declaration (a class can
// never be left uninitialized). Stops at pointer/iterator (a reference leaf may
// be null) and at primitives/void; sees through alias/const.
bool hasInPlaceClass(TypeRef ref);

// Stage-0 round-trip self-test over a fixed vocabulary; true on success.
bool typeSelfTest(std::ostream& out);

// Returns false if the slids type isn't a numeric primitive (e.g. void, char[]).
bool classify(std::string const& slids_type, TypeKind& out);

// True for any recognized type spelling: numeric primitives, void, T[] (recursive).
// Classify uses this to validate declared / return type spellings up front.
bool isKnownType(std::string const& slids_type);

// Byte size of a type spelling, or -1 if not statically known. A reference /
// iterator / nullptr is a pointer (8); a fixed array is its TOTAL bytes (product
// of every dimension times the element size). A slid type's size is unknown
// until its layout (Phase 5) / link (cross-TU) — returns -1. Used by sizeof.
long long typeByteSize(std::string const& slids_type);

// Literal fit checks. Report a diagnostic and return false if the literal
// doesn't fit in dest_type. Int literal targeting a float type requires exact
// representability against the float's significand; float literal targeting
// an int type requires the value to be integer-valued and in range. The
// (file_id, tok) attribute the error to the literal's source token.
bool checkIntLiteralFits(std::string const& literal_text,
                         std::string const& dest_type,
                         int file_id, int tok,
                         diagnostic::Sink& diag);

bool checkFloatLiteralFits(std::string const& literal_text,
                           std::string const& dest_type,
                           int file_id, int tok,
                           diagnostic::Sink& diag);

// A literal whose NOMINAL type is `nominal` silently widens to `target` when both
// are integer-class and `target` is strictly wider (canon: "integer-class literals
// may be silently widened to any LARGER integer-class type"). This is the sole
// gate that admits the upper-bits case: `~0x0F` has nominal uint8 but a value
// (0xFF..F0) exceeding int64's positive range — the widen is legal on the nominal
// even though the value would fail a magnitude fit, because the widen truncates
// the bit pattern directly. A same-width nominal (uint64 -> int64) is NOT wider,
// so a genuine large literal like 9223372036854775808 is still rejected.
bool nominalWidensTo(TypeRef nominal, TypeRef target);

// Silent variants used by the binary-op literal-flex rule.
bool intLiteralFits(std::string const& literal_text, std::string const& dest_type);
// char's RANGE check alone (8-bit code point) — for constfold's char-arithmetic
// absorption, where the operand IS a char; intLiteralFits deliberately rejects
// char dests (an integer literal never matches char).
bool charLiteralRangeOk(std::string const& literal_text);
// Loud fit for a CHAR literal: exact against char, widens out as an 8-bit
// unsigned; reports and returns false otherwise.
bool checkCharLiteralFits(TypeRef dest, int file_id, int tok,
                          diagnostic::Sink& diag);
bool floatLiteralFits(std::string const& literal_text, std::string const& dest_type);
// TypeRef overloads — classify the dest directly (sees through kAlias); kNoType fits.
bool intLiteralFits(std::string const& literal_text, TypeRef dest);
bool floatLiteralFits(std::string const& literal_text, TypeRef dest);

// TypeRef overloads — kNoType is the empty-dest ("no target, always fits") case.
bool checkIntLiteralFits(std::string const& literal_text, TypeRef dest,
                         int file_id, int tok, diagnostic::Sink& diag);
bool checkFloatLiteralFits(std::string const& literal_text, TypeRef dest,
                           int file_id, int tok, diagnostic::Sink& diag);

// Variable-to-variable conversion. Emits any needed LLVM op
// (sext/zext/fpext/sitofp/uitofp) and returns the new value name. On
// disallowed conversion reports a diagnostic attributed to (file_id, tok)
// and returns the original value as a fallback.
std::string convert(std::string const& src_val,
                    std::string const& src_type,
                    std::string const& dest_type,
                    int file_id, int tok,
                    std::ostream& out,
                    diagnostic::Sink& diag);

// TypeRef overload — kNoType maps to the empty-spelling ("no conversion") case.
std::string convert(std::string const& src_val,
                    TypeRef src, TypeRef dest,
                    int file_id, int tok,
                    std::ostream& out,
                    diagnostic::Sink& diag);

// Explicit value conversion — the `(Type=expr)` grid. Unlike `convert` (implicit
// widening; rejects narrowing / cross-family / sign-change), this permits the
// whole grid with C semantics and NEVER reports — classify has already validated
// the source/target. Emits the op (trunc/ext/fp<->int/sign reinterpret/nonzero
// test) and returns the new value. A pointer source is allowed only to `bool`
// (non-null test) or `intptr` (ptrtoint); the target is never a pointer.
std::string convertExplicit(std::string const& src_val,
                            TypeRef src, TypeRef dest,
                            std::ostream& out);

// "Smallest type large enough to hold either operand" per the widen.sl binary
// rule. Returns false if no built-in type fits both. The TypeRef overload is the
// operands stripped; result is the preferred handle.
bool commonType(TypeRef t1, TypeRef t2, TypeRef& out);

}  // namespace widen
