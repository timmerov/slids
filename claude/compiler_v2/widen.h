#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace diagnostic { struct Sink; }

namespace widen {

enum class Category { kBool, kSignedInt, kUnsignedInt, kFloat };

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
    int slid_entry_id = -1;                // kSlid (resolved later)
    bool needs_ctor = false;               // kSlid: has a constructor hook to call
    bool needs_dtor = false;               // kSlid: has a destructor hook to call
};

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

// A named class/slid type carrying its field-slot types. Interned by name (one
// handle per class); resolve calls this once the field list is known to attach
// the layout. Codegen reads get(ref).slots for the struct definition.
TypeRef internSlid(std::string const& name, std::vector<TypeRef> const& slots);

// Mark a class's lifecycle hooks on its (already-interned) kSlid type, so
// codegen knows where to emit constructor / destructor calls.
void setSlidLifecycle(std::string const& name, bool needs_ctor, bool needs_dtor);

// Peel any alias layers, returning the first non-alias handle (the underlying
// structure). Predicates that switch on form() use this to see through aliases.
TypeRef strip(TypeRef ref);

// Recursively remove EVERY alias (at all levels), rebuilding wrappers — so two
// types that are equal modulo aliases (`Integer^` and `IntPtr = int^`) deep-strip
// to the SAME handle. Use for type-equality across alias boundaries (ptr casts,
// same-pointee checks).
TypeRef deepStrip(TypeRef ref);

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

// Silent variants used by the binary-op literal-flex rule.
bool intLiteralFits(std::string const& literal_text, std::string const& dest_type);
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
// rule. Returns false if no built-in type fits both.
bool commonType(std::string const& t1, std::string const& t2, std::string& out);

}  // namespace widen
