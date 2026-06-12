compiler_v2 — slids compiler rewrite, in-progress

(Companion docs: plan.txt = the phase roadmap / main quest; todo.txt = open
side-quests — bugs, reach goals, deferrals. This file = per-stage current-state
notes: what each stage does today.)

PIPELINE (each stage consumes its predecessor's output, produces its successor's input):

    source text
      => lex       => token::List
      => numeric   => token::List (literal tokens canonicalized + validated)
      => grammar   => parse::Tree
      => resolve   => parse::Tree annotated (symbol refs; const idents substituted)
      => constfold => parse::Tree (literal sub-trees collapsed + nominal-typed)
      => classify  => parse::Tree annotated (types: inferred_type + op_type)
      => desugar   => ast::Tree (mangled names baked in by functionSymbol)
      => optimize  => ast::Tree annotated (perf rewrites in place)
      => codegen   => .ll text

main.cpp drives the chain per TU and runs `diagnostic::hasErrors(diag)` between
each stage to short-circuit on errors. There is no separate layout stage: LLVM
owns the struct layout (codegen GEPs fields by INDEX, sizes via the GEP-null /
ptrtoint helper), name mangling happens at the parse->ast seam (desugar), and the
per-class derived facts (needs_ctor/dtor) are computed in resolve.

DESIGN PRINCIPLES

  * Stages = code (executive decisions). Products = data + ALL manipulation APIs.
    Stage files never touch storage directly; they only call product APIs.
  * Three product types only: token, parse, ast. No side tables.
  * One error pipe: diagnostic::report. Error rendering may cross stage
    boundaries (it reads token::List to look up source attribution).
  * Lean unwind, no throw. Stop at first error today; sink can grow an
    N-error threshold without changing report sites.
  * No silent defaults. Every default arm is documented no-op, unreachable
    assert, or error path. Enforced by `-Werror -Wswitch-enum`.
  * One assignment relation. Every assignment-like operation — decl-init,
    assign, store, move, call/method args, return, field-init, and the
    element/slot pairs from destructure + array/tuple recursion — routes
    through ONE check (classify) and ONE lowering (codegen). Swap is the lone
    exception (exact type). See ASSIGNMENT RELATION.

TYPE REPRESENTATION (the carrier; not a stage)

  * Types are STRUCTURED, never strings. A type is a widen::TypeRef — a handle
    into a process-lifetime interned arena (widen::intern / widen::spell). A
    widen::Type carries a Form (kNone / kPrimitive / kVoid / kAnyptr / kPointer /
    kIterator / kArray / kSlid / kTuple / kAlias) plus its payload (cat+bits,
    pointee, elem+dims, slots, underlying). spell(intern(s)) == s exactly (bar
    kAlias, which is minted not parsed), guarded by `slidsc --type-selftest`.
  * Dedup is STRUCTURAL (by_struct, keyed on form + child handles), NOT by
    spelling — int / int32 / intptr still stay DISTINCT (their spellings differ)
    yet share cat()/bits(), but an alias-bearing composite (`(Dir,bool)`, `Dir^`)
    is now distinct from its kSlid-leaf form, which a spelling key could not do
    (grammar interns spellings with kSlid leaves BEFORE resolve knows they are
    aliases). intern(spelling) parses then structurally interns; by_spelling is
    only a parse memo. Structural constructors build a composite from child
    HANDLES: internPointer / internIterator / internArray / internTuple, and
    internAlias(name, underlying) — the last minted ONLY by resolve (which has the
    symbol table). strip() peels one alias layer; deepStrip() removes all (so
    `Integer^` and `IntPtr=int^` compare equal modulo aliases).
  * kAlias is a TRANSPARENT type: spells as its name (for ##type/diagnostics) but
    sees through to `underlying` for every structural query (classify / llvm /
    size / known / the form-predicate cluster via strip). Aliases + enum type
    facets are kAlias; alias_label is now a derived display cache, not a channel.
  * Every type FIELD is a TypeRef: Node.return_type / inferred_type / op_type /
    nominal_type / strong_type and Entry.slids_type / const_strong_type /
    param_types / capture_types (both parse:: and ast::), plus codegen::VarInfo.
    alias_label stays a std::string — it is a display NAME, not a type.
  * Structure is the single source of truth. Spellings are RENDERED on demand
    ONLY at genuine edges: diagnostics, ##type, the no-width commonType rule, and
    the classify primitive-name lexer. NEVER cache a canonical spelling — storing
    a type-string is what killed v1. codegen + print + the classify/resolve
    predicate+cast-rule cluster read structure off the handle (form/cat/bits);
    upstream stages that still compute spellings bridge at field boundaries via
    widen::spellOrEmpty (read) / widen::internOrNone (write).
  * kNoType is the "no type" handle (empty spelling). get(kNoType) reads as
    Form::kNone, so every predicate returns false/none on it AND a form==kVoid
    test can never mistake a no-type for void — a stray no-type surfaces loudly
    (e.g. llvmForRef assert) rather than silently lowering as void.

ASSIGNMENT RELATION (the one implicit-conversion matrix; spans classify + codegen)

  The single relation governing whether a source value may flow into a target
  slot, and with what implicit conversion. Rows = target, columns = source.
  Each cell is: a delegated sub-rule, a terminal accept, recurse, op=, or error.

    target \ source | integer | float | pointer | nullptr | tup/arr | class
    ----------------+---------+-------+---------+---------+---------+------
    integer         | widen   | error | error   | error   | error   | error
    float           | error   | widen | error   | error   | error   | error
    intptr          | widen   | error | accept  | accept  | error   | error
    pointer         | error   | error | ptr     | accept  | error   | error
    void^           | error   | error | accept  | accept  | error   | error
    tuple / array   | error   | error | error   | error   | recurse | error
    class           | op=     | op=   | op=     | op=     | op=     | op=

  * widen  — the established numeric rules: a weak literal flexes; a strong-const
    or typed value widens WITHIN family only (narrow / sign-cross / int<->float
    cross-family reject). intptr is in the integer family here.
  * ptr    — pointer rules (classify ptrImplicitOk): a typed pointer target needs
    a MATCHING pointee, or an iterator->reference demote of the same pointee.
    Unrelated pointees are an error (an explicit cast is required).
  * accept — terminal. intptr <- pointer/nullptr lowers to ptrtoint; pointer/
    void^ <- nullptr is the null store. void^ <- ANY pointer is the universal
    erase (the `to == void^` arm) — accept-in, cast-out: a void^ SOURCE into a
    typed pointer falls under `ptr` and is rejected.
  * recurse — the aggregate x aggregate block: match slot/element count, then each
    element/slot pair RE-ENTERS this matrix. Self-similar to any depth.
  * op=    — the class row delegates wholly to the class assignment operator. Not
    landed, so every class-target cell is an ERROR until op= is defined/synthesized.
  * error  — everything else.

  intptr-as-source falls in the integer column, so `pointer <- intptr` is an error
  (implicit); the intptr<->pointer accept is one-directional (intptr <- pointer).

  Construction (a class ctor at a decl / `new`) is a SEPARATE operation, not this
  matrix — class <- tuple as a ctor lives outside it. Swap (`<-->`) is also outside:
  it requires the EXACT same type both ways, since it cannot convert both directions.

  INTEGER (the `widen` cell, integer family) -- per target, the source kinds
  accepted. Shorthand: N = the row's width, M = any valid width < N (signed widths
  8/16/32/64; unsigned 1/8/16/32/64, where uint1 = bool, uint8 = char). intM / uintM
  = the SET of signed / unsigned types narrower than N; intN / uintN = width N.
  flexK / uflexK = a literal whose single nominal type is intK / uintK. Folded:
  char = uint8, int = signed 32, uint = unsigned 32, intptr = signed 64 (int64 ==
  intptr). int = the intN row at N=32, uint = the uintN row at N=32.

    target |  N      | accepted
    -------+---------+----------------------------------------------------
    bool   |  1      | bool, uflex1 ¹
    char   |  8      | bool, char, uint8, uflex1, uflex8, flex8 (0..127) ²
    int8   |  8      | bool, int8, uflex1, flex8
    intN   |  16..64 | bool, char, intM, intN, uintM, flexM, flexN, uflexM
    intptr |  64     | bool, char, intM, intN, uintM, flexM, flexN, uflexM
    uintN  |  8..64  | bool, char, uintM, uintN, uflexM, uflexN, flex+ ³

  Inferred targets -- the source passes through to its own spelling (a weak literal
  presents at the preferred no-width spelling when it fits the 32-bit default, else
  at its nominal width):

    target          |  N      | accepted
    ----------------+---------+----------------------------------
    inferred bool   |  1      | bool
    inferred char   |  8      | char
    inferred int8   |  8      | int8
    inferred int    |  32     | int, flexM, flex32
    inferred uint   |  32     | uint, uflexM, uflex32
    inferred intN   |  16..32 | intN
    inferred uintN  |  8..32  | uintN
    inferred int64  |  64     | int64, flex64
    inferred uint64 |  64     | uint64, uflex64
    inferred intptr |  64     | intptr

  * uflex1 -- literal with nominal uint1 (value 0 or 1); the only literal that fits
    bool. On the wider rows it is just the smallest uflexM.
  * flex8 (0..127) -- flex8 is signed int8 (-128..127); only its non-negative half
    fits char (unsigned 8). A value-clip, not a width boundary.
  * flex+ -- a signed-kind literal reaches an unsigned target only by VALUE (non-
    negative, in range): `uint x = 5` ok, `uint x = -1` errors. The nominal category
    cannot express it.

  int8 stands apart from intN: at N=8 it accepts neither char (uint8 -> int8 is a
  same-width unsigned -> signed, rejected) nor any narrower width -- only bool,
  itself, flex8, and uflex1. Every intN with N>8 admits char outright. The width
  cells compress into M<N; the three footnoted cells are the value/sign seams where
  a nominal category stops lining up with a width boundary.

  FLOAT (the `widen` cell, float family) -- per target, the source kinds accepted;
  for an INFERRED target, the sources that resolve to that spelling. float / float32
  / float64 are the strong types (no-width / 32 / 64); flex is a weak float literal.

    target           | accepted sources
    -----------------+----------------------------------------
    float32          | flex32, float, float32
    float64          | flex32, flex64, float, float32, float64
    inferred float   | flex32, float
    inferred float32 | float32
    inferred float64 | flex64, float64

  * flex32 -- a float literal whose value fits float32 (nominal type float32).
  * flex64 -- a float literal whose value is too large for float32 (nominal type
    float64).

  POINTER (the `ptr` cell, expanded) -- keys on the POINTEE. Shorthand: T = the
  target's pointee; U = any DIFFERENT pointee (U != T). Kinds: T^ reference, T[]
  iterator, void^ the universal reference, intptr the integer bridge, nullptr the
  null literal (internally anyptr).

    target | accepted
    -------+--------------------------------------------------
    T^     | T^, T[], nullptr
    T[]    | T[], nullptr
    void^  | T^, T[], U^, U[], void^, nullptr   (= any pointer)
    intptr | T^, T[], U^, U[], void^, nullptr   (= any pointer) ⁴

  Inferred target -- the source passes through to its own type:

    inferred T^    | T^
    inferred T[]   | T[]
    inferred void^ | void^

  * iterator -> reference is one-way: T[] -> T^ accepts (demote, loses arithmetic),
    so T[] is in the T^ row; the reverse T^ -> T[] rejects (no implicit arithmetic
    gain), so T^ is absent from the T[] row.
  * pointee must match: U^ / U[] reject for T^ / T[]; only void^ / intptr take any
    pointee (U^ -> T^ needs an explicit cast, the chain-through-void^ rule).
  * void^ is accept-in, cast-out: any pointer erases TO void^, but a void^ SOURCE
    into a typed T^ / T[] falls under the pointee rule and rejects. Only void^ is the
    implicit universal sink; int8^ / uint8^ -- "buffer-class" for EXPLICIT casts and
    placement-new -- are ordinary T^ here.
  * ⁴ intptr <- pointer is ptrtoint, one-way: accepts any pointer; the reverse
    pointer <- intptr is not implicit (integer column, rejects).
  * nullptr is source-only -- it fits any pointer target but, having no pointee,
    cannot drive an inferred declaration, so it has no inferred row.

  TUPLE / ARRAY (the `recurse` cell) -- no grid: one rule covers all four
  aggregate x aggregate combos (tuple<-tuple, array<-tuple, tuple<-array,
  array<-array):

    SHAPE must match, and each slot/element pair must be COMPATIBLE -- every pair
    RE-ENTERS this matrix, source-slot -> target-element.

  Shape = same slot/element count (and matching dims for multi-dim arrays / nested
  tuples). Compatibility is DIRECTIONAL, like the scalar matrices:

    (bool, char, int8, uint16) -> int[4]   works  -- each slot widens to int
    int[4] -> (bool, char, int8, uint16)   rejects -- each int narrows / sign-flips

  * scalar <-> aggregate is an error (the aggregate/scalar boundary).
  * per-element compatibility means an aggregate assign LOWERS to per-element
    assignments, each running its own conversion (the tuple->int[4] above is four
    distinct converts) -- NOT a whole-aggregate copy. A single memcpy is only the
    degenerate case where every element is already identical. The current exact-
    deepStrip array-value rule (checkArrayValueAssign) is the known-wrong seam: it
    rejects `int8[4] -> int[4]`, which this rule accepts.

ANONYMOUS TUPLES + #x (landed this phase; spans every stage)

  * A tuple is Form::kTuple (slots). LITERAL `(a,b,...)` is kTupleExpr (grammar:
    parsePrimary, comma after the first paren expr; size-1 collapses to the bare
    expr). TYPE `(T,T,...)` parses in parseType; a `(`-led statement disambiguates
    via looksLikeTupleTypeDecl (trailing name) / looksLikeTupleDestructure (`)=`).
  * Landed: construct + whole-copy + const-index read `t[k]` (extractvalue; a
    RUNTIME index on a tuple is rejected — heterogeneous slots); slot write
    `t[k]=v` (struct-GEP) + destructure `(a,b,)=t` (kDestructureStmt, null child =
    skipped slot); slot-wise arith + scalar broadcast (`(1,2,3)+7`); params /
    returns / references (`{i32,i32}` by value, `(T,T)^` = ptr). Codegen builds
    the aggregate via insertvalue; classify slot-types via internTuple.
  * ARRAY from a tuple: `int a[3]=(1,2,3)`, `a=(4,5,6)`, multi-dim
    `int td[3][2]=((1,2),(3,4),(5,6))` (a NESTED tuple whose SHAPE — row × col —
    matches the standard-order dims). ELEMENT-AWARE: collectArrayElementNodes
    descends EXACTLY dims.size() levels and stops at the ELEMENT — so a scalar
    element flattens to leaves, but a tuple/array element stays an aggregate (this
    is how ARRAYS OF TUPLES `(int,int) a[3]` and TUPLE-OF-ARRAYS slots work; it
    replaced the old flatten-to-scalars + tupleMatchesArrayShape). A wrong nesting
    is "Array initializer shape does not match the dimensions of '<T>'"; each leaf
    widens into the element type. Also: array↔tuple VALUE copy both directions
    (`(int,int,int,int) t = a1` / `int a4[4] = t4`, extractvalue/insertvalue, per-
    slot widen); PARTIAL-index lvalues (sub-array assign `td[1]=(100,101)` + sub-
    array value read). See [[project_v2_array_types]].
  * ARRAY TYPES (`int[N]`): parseType parses sized dims (LITERAL only in type
    position), so array types compose — tuple slots `(int[3],int[4])`, alias RHS,
    params (via alias), returns `int[3] f()`. Variable decls stay name-anchored
    `int x[3]` (reject_array_dims rejects a top-level sized-array type at decl
    sites). INDEXING is a per-segment walk in emitElementAddr: dispatch on the
    CURRENT type each step (array dim -> GEP; tuple slot -> struct GEP), composing
    alias-element nested arrays + array->tuple->array chains; the old codegen rank
    check (a buggy duplicate of classify's per-level over-index) was deleted.
  * FOR-TUPLE `for (v : tuple)` over a HOMOGENEOUS tuple: resolve understands it
    (understandForTuple, retagging the kForEnumStmt carrier to kForTupleStmt) and
    desugar lowers it (lowerForTuple) to a kForLongStmt walking an iterator
    `_$iter = <T[]><void^>base` (the void^ bridge — so a `ref^` deref iterable
    dodges addr-of-through-deref); a VARIABLE iterates in place (no copy, mutable
    by-ref writes back), a LITERAL or rvalue call spills to a `_$ftmp`. A
    non-primitive element forces by-ref; a by-ref var's pointee must match the
    element type.
  * #x desugars (grammar parseUnary) to the 5-tuple `(##file, ##line, ##type(x),
    ##name(x), ^x)`; x must be an lvalue. Passing an rvalue tuple to a reference
    param (`dump(#x)`) materializes it in a temp — emitCall brackets such a call
    in @llvm.stacksave/stackrestore so a materializing call in a loop doesn't leak.


CLASSES + CTOR/DTOR (landed this phase; spans every stage)

  * A class IS a named tuple. `Name(field-list){body}` (grammar parseClassDef: an
    identifier directly followed by `(` at file scope — a function is `Type name(`).
    The field list reuses the param-list parser (kParam nodes, defaults in
    children[0]). resolve registerClass builds a ClassInfo (field names / types /
    the stable kParam nodes / def location) and interns the kSlid type CARRYING its
    field slot types (widen::internSlid) — so the whole tuple aggregate path
    (construct, store, slot access, llvmForRef -> literal struct) is reused and
    codegen needs NO symbol table; the layout rides on the type. requireKnownType
    accepts a registered-class leaf.
  * `.field` is a kFieldExpr (grammar postfix `.name`); classify types it via the
    ClassInfo; desugar lowers it to a kIndexExpr over the field's slot index, so it
    never reaches codegen (slot access by name). `^field` address-of walks
    kFieldExpr chains (resolve) and emitElementAddr GEPs a kSlid slot.
  * CONSTRUCTION (classify classifyClassInit) normalizes every init form to a
    per-field tuple: each field = init-tuple slot, else the author default (read
    LIVE off the kParam node — constfold may have replaced it), else zero
    (classZeroValue: 0 / 0.0 / false / nullptr; an unhandled field type errors). A
    class-typed field constructs RECURSIVELY (constructClass): a scalar/tuple is the
    sub-class's ctor input filled with ITS defaults; a same-class value copies. The
    `=` form SPREADS its tuple slot-to-field; the call form keeps each arg whole. A
    size-1 init tuple is inexpressible (`( x )` collapses) — punted (todo). A class
    var-decl is definitely-initialized (DA) even with no initializer.
  * CTOR/DTOR are scope HOOKS, not the constructor — fields are initialized first,
    the ctor runs after, the dtor at scope exit. `_(){}` / `~(){}` parse as
    kFunctionDef with an implicit `self` (`Name^`) param; a bare field name in the
    body rewrites to `self^.field` (resolve method_fields fallback — locals shadow).
    desugar lifts them to top-level `<Name>__$ctor` / `__$dtor`. Optional but must
    PAIR; FORWARD declarations (`_();`) allowed but must be defined; no author
    params. The kSlid type carries has_ctor/has_dtor (the explicit symbol exists)
    vs needs_ctor/needs_dtor (TRANSITIVE).
  * CALL-IF-NEEDED + ITANIUM RECURSIVE DESCENT: a trivial class emits no calls.
    needs_ctor/needs_dtor is transitive over the field graph (resolve fixpoint after
    all classes register — a by-value field whose class needs hooks propagates up).
    emitConstructHooks runs each class-typed field's hooks in declaration order then
    the class's own ctor; emitDestructHooks runs the class's own dtor then field
    dtors in REVERSE, to any depth. The unused-local sweep exempts a hook-bearing
    class (the instance IS the use). [DEFERRED: an array-/tuple-of-hook-class field —
    the fixpoint + walkers descend only DIRECT kSlid fields.]
  * DESTRUCTOR-BALANCE INVARIANT: every instance destroyed once, in reverse
    declaration order, on EVERY exit. A DtorScope chain is threaded through emitStmt
    alongside the LoopCtx: a needs_dtor var registers in the current scope; a block
    emits its scope's dtors at normal fall-through end (skipped when the block ends
    abrupt — endsTerminated, so nothing is emitted after a terminator); a `return`
    unwinds the whole chain (the value is materialized first); `break`/`continue`
    unwind down to the target loop's boundary scope (LoopCtx.scope). [DEFERRED
    tests: the return/break/continue arms + the loop-VARIABLE case.]


CLASSES: NEW / DELETE / SIZEOF + .~() (landed this phase; spans every stage)

  * SIZEOF(Class) — LLVM owns the struct layout, so a class's size is NOT a
    compile-time constant. codegen emits a per-class `define internal i64
    @<Name>__$sizeof()` = `getelementptr <struct>, ptr null, i32 1` + `ptrtoint`
    (v1's design — resolves at link time for cross-TU). The helper symbol is
    widen::classSymbol(handle) (bare name for file-scope, disambiguated for a
    local). resolve recognizes a bare class name as a TYPE operand (its kClass
    entry redirects to the registered kSlid); classify rewrites `sizeof(Class)` to
    a CALL of that helper (a runtime intptr, NOT foldable — can't init a const).
    The class kSlid types are threaded parse->ast via a new `ast::Tree.classes`
    (desugar populates it).
  * NEW T / NEW T(args) — a class is sized by `call @<Name>__$sizeof()` (not the
    typeByteSize literal). `new T(args)`: grammar parses the trailing `(args)` onto
    kNewExpr children[2] (distinct from the leading `new(addr)` placement and `[n]`);
    classify routes it through constructClass (the same field-init tuple as a class
    var-decl); codegen mallocs, field-inits the construction tuple at the pointer,
    then emitConstructHooks runs the ctor. PLACEMENT `new(addr) T(args)` reuses the
    same construct at the buffer address (no malloc).
  * NEW T[n] (the new[] COOKIE) — a class array always field-inits each element (the
    default value laid into the slot); a HOOK class additionally prepends an 8-byte
    count COOKIE (the returned pointer is malloc+8) and runs the ctor per element.
    The cookie + ctor-hook gate on needs; the field-init does not (a trivial class
    still has field defaults). A primitive array stays a plain malloc.
  * DELETE — single (T^): null-guarded dtor (free(null) is safe; the dtor on null
    derefs), then free. Array (T[]) of a hook class: read the count at ptr-8, run the
    dtor on each element in REVERSE, free ptr-8. A primitive / trivial-class pointer
    is a plain free. Gated on typeNeedsHook(pointee).
  * EXPLICIT DESTRUCTOR `lvalue.~()` — a kDtorCallStmt (grammar: `.` then `~` in the
    name-led lvalue chain — a destructor call, not a field). codegen runs
    emitDestructHooks on the receiver's address with NO free / no null (placement
    cleanup; the buffer is reclaimed separately). classify requires a class receiver.
    (Double-destruct of a scope-managed value via `value.~()` is the author's
    problem — not guarded.)
  * FIELD ACCESS through deref / iterator — `.field` lowers to a slot kIndexExpr;
    emitElementAddr's per-segment walk roots on a variable's alloca OR a deref `ptr^`
    (the pointer value), and an ITERATOR step loads the sequence pointer + GEPs by
    element. So `ptr^.field`, `iter[i].field`, and `arr[i].field` compose for any
    field/shape, READ or WRITE. The name-led lvalue chain also parses `.field` as a
    store target (field WRITES, previously only ctor-body `self^.field`).


CLASSES: AS A NAMESPACE + LOCAL (defined in a function body) (landed; spans stages)

  * A CLASS IS ALSO A NAMESPACE. Every class gets a `kClass` ENTRY (a new
    EntryKind) carrying an ns_frame_id (its member set) AND its kSlid as
    slids_type (so the name is a type too). Its body holds member DEFINITIONS —
    aliases, consts, enums (NOT free functions). Qualify them by the class name:
    `Space:Float` (a member type-alias), `Space:kPi` (a member const),
    `Space:Count:kOne` (an enum member — the enum keeps its name in the path). A
    type-alias to a class sees through to both facets (`alias Time = Space;` then
    `Time:Count` (type) and `Time:Count:kZero` (value) both resolve). `alias
    Space;` (bare namespace import) is REJECTED — a class is a type, not an
    importable namespace. resolveNamespaceSegments accepts a class frame and
    follows an alias to it; resolveQualifiedType accepts a member alias as a type
    leaf; a member type names itself qualified for ##type (memberQualifiedName).
  * LOCAL CLASSES — a class may be defined in ANY function body, and in any nested
    block within one (if / else / loop / switch case). resolveStmtList runs a
    local-class pre-pass over EACH scope's statements before resolving them, so a
    use may precede the definition in that scope, and the class registers in that
    block's frame (drops at scope exit). Shadowing falls out of innermost-first
    lexical lookup — a local class shadows any same-named class in an enclosing
    scope, local or file-scope; the shadowed one is unreachable (`::` is global;
    see todo.txt). resolveTypeRef redirects every class-name reference to the one
    registered kSlid HANDLE. A local class is a FULL class: members, a hook-class
    field (whose ctor/dtor run — see below), sizeof, new/delete; its ctor/dtor
    lift to module-level functions like a file-scope class's.
  * HOISTED CLASSES — a class defined in another class's BODY is a namespace-MEMBER
    of the host (like its alias/const/enum members), reached as `Outer:Inner` (and
    `Outer:Inner:Innerger`, any depth). registerClassMembers gains a kClassDef arm
    (registerClassName/Body with member_of = the host frame; def_id = host frame, so
    `Outer:Inner` is a distinct identity); resolveQualifiedType accepts a kClass
    leaf and returns the HANDLE via an out-param (NEVER a spelling round-trip — a
    class's def_id can't survive one). A hoisted class is NOT bound to a host
    object: it sees the host's namespace members (`Inner`'s body reads `Outerger`
    bare, `Outer:Outerger` qualified) but NOT the host's fields (a bare host field
    is Unresolved). classify recurses into hoisted ctor/dtor (classifyClassMemberBodies).
  * SCOPE-AWARE MEMBER RESOLUTION (name-then-resolve, frames open) — NOT a leniency.
    A type name resolves via resolveName (open-ns chain + lexical-with-owner<0), NOT
    findInLiveScopes (any live entry). For that to be correct, member TYPES must
    resolve with the enclosing frame OPEN: registerNamespaceTree / registerClassMembers
    / registerClassBody / resolveClassMemberBodies each push their frame around the
    member-type / body resolution (names were registered first — type-introducing
    members before consts — so forward refs resolve). Result: a member type resolves
    bare only where its frame is open (inside the host); a bare member type at file
    scope FAILS — "'Inner' needs a namespace qualifier" when it IS a member-type
    elsewhere (namespaceMemberTypeExists — class/alias/enum, NOT a const/function),
    else "Unknown type". (This replaced the findInLiveScopes leniency that resolved
    any live member regardless of scope.)
  * IDENTITY BY def_id, NOT A MANGLED NAME. Two same-named local classes (or a
    local shadowing a file-scope one) must be distinct types. The kSlid carries a
    `def_id` (its defining FRAME id; -1 for file-scope) included in structKey
    (`"S"+name` for file-scope, `"S"+name+"#"+def_id` for a local), so they intern
    to distinct handles while the NAME stays bare everywhere a human or a map sees
    it (diagnostics, ##type, spell()). tree.classes is keyed by the kSlid HANDLE
    (not a name string). The ONE place a class name is disambiguated is
    `widen::classSymbol(handle)` — bare name for file-scope (IR unchanged), `name
    + ".<frame>"` for a local — minted at desugar (ctor/dtor defs) and codegen
    (calls / sizeof) so def and use agree, and living only in emitted LLVM. (This
    is the "defer name mangling to codegen" rule: scope is disambiguated by the
    entry-id/frame stack, never by a stored canonical name — v1's fatal trap.)
  * TWO-PHASE REGISTRATION (so a field may FORWARD-reference a sibling class).
    registerClassName (Phase 1) makes every class name a known type — a kClass
    entry + a SLOTLESS interned handle + a placeholder ClassInfo — then
    registerClassBody (Phase 2) resolves field types (forward refs now validate)
    and attaches the slots. The kSlid handle is stable slotless->slotful (structKey
    excludes slots), so a field that referenced the forward class shares the very
    handle that later gains its layout. File scope: Pass 1a-class is a names loop
    then a bodies loop. Local scope: registerLocalClasses does both phases per
    sibling set (idempotent across the top-of-body / resolveStmtList pre-passes).
  * BY-VALUE CYCLE CHECK (checkClassByValueAcyclic). A class whose by-value field
    graph cycles back to itself has INFINITE size — classify's recursive
    construction and codegen's struct lowering would recurse forever (a SIGSEGV).
    After Phase 2, a DFS over by-value field deps (kSlid, or an array / tuple of
    one; a `^` / `[]` breaks the cycle) rejects `Foo(Foo f_)`, mutual `A(B)`/`B(A)`,
    array/3-hop cycles. (The two-phase made these reachable; before, the forward
    name was simply Unknown.)
  * TRANSITIVE LIFECYCLE FOR A LOCAL CLASS. The file-scope needs-fixpoint runs
    before any body resolves, so a local class isn't swept by it. registerLocalClasses
    runs the same fixpoint over its sibling set after Phase 2, so a local
    `Outer(Inner i_)` (forward or not) runs Inner's ctor/dtor.
  * CLASS-VALUE ASSIGNABILITY (checkSlidAssign). A class value is assignable only
    to the SAME class. classify's checkSlidAssign is the terminal reject the
    assign/decl/call/return dispatch otherwise lacked: a class meeting a primitive
    or a different class -> "Cannot implicitly convert ...". Runs at var-decl,
    assignment, call ARGUMENT (both the single-candidate and multi-overload paths),
    and RETURN. (Same-class is a fine copy; pointer cases are checkPtrAssign's;
    two non-classes flex per codegen's numeric rules.)
  * METHODS — a named function in a class body is a method: a ctor/dtor with a user
    name. Grammar parses it like any function and injects an implicit `self`
    (Class^) at params[0]; the body is a FULL function body (local consts/classes,
    unused-local sweep, nested functions, return checks), with bare field names
    rewritten to `self^.field` for READS and WRITES (buildSelfField — one place mints
    `self^.field`, shared by the kIdentExpr read path and the assignment-LHS write
    path; a bare `x_ = v` becomes a self^.field store, not a phantom local). resolve/
    classify reach a method through the SAME ctor/dtor sites (the forEachHoistedClass
    walker, now filtered on kFunctionDef). The method entry's param_types hold the
    FULL list (self at [0], aligned with the node's params so the resolved-type
    write-back stays index-correct); it lifts to `<Class>__method(self, ...)`. A call
    `obj.method(args)` parses to kMethodCallStmt (children[0] = receiver); classify
    resolves the method via the receiver's class frame (classEntryForType +
    findMemberDeclared — the shared member lookup), type-checks args against
    param_types[1..], and threads the method entry id; desugar lowers it to a normal
    call of the lifted symbol minted from the method's OWN DEFINING class (not the
    receiver's, so an inherited call will name the base), with the receiver's address
    prepended as self (for `ptr^.m()` self IS the pointer — an addr-of of a deref is
    not a codegen lvalue). A bare call resolving to a method errors ("Method 'm' must
    be called on an object.") — sibling calls via `self` are deferred. Statement form
    only; expression form + compound field writes (`x_ += 1`) are todo.txt.
  * NAME COLLISIONS + TYPE-NAME DIAGNOSTICS. A class name collides with ANY
    same-name entry (another class, an alias / enum / namespace, a const, a
    function) — reportNameCollision carets the source-LATER declaration as the
    duplicate (registration order need not match source order, so it compares
    positions). requireKnownType, when a type name resolves to a non-type entry,
    reports "'X' is a namespace / function / constant / variable, not a type"
    instead of a blunt "Unknown type" (the precise form fires where the entry is
    already registered — e.g. a body var decl; a class FIELD validates before
    file-scope functions/consts register, so those stay "Unknown type").


STAGE FILES (.h / .cpp pairs)

  lex       text -> tokens. Wraps the scanner in an ImportWrapper that
            recursively expands `import X;` at depth-0 file scope into one
            unified token list. Tracks bracket-kind balance ( { [ only.
            Emits kEndOfFile per file, kEndOfInput once at the outermost
            return. Numeric literals: strips underscores, emits source-form
            text per kind (char/int/uint/float); rejects only structural
            errors (missing digits after 0x / 0b / e). Value parsing,
            escape interpretation, and overflow live in the numeric stage.
  numeric   tokens -> tokens. Validates and canonicalizes every literal
            token in place: char escapes ('A' -> 65, '\\n' -> 10); hex/
            binary -> decimal (0xFF -> 255, 0b1010 -> 10); float text via
            %.17g; bool "true"/"false" -> "1"/"0". Overflow assumes uint64
            / float64 storage; kIntLiteral whose value > INT64_MAX flips
            kind to kUintLiteral here. Codegen's float32-emit path now
            uses the hex bit-pattern form (item 7 landed) so lossy values
            like `3.14` reach llc successfully. One open item remains on
            the migration table — proper attribution for uint/char
            literal-fit errors.
  grammar   tokens -> parse tree. Pure syntax; every identifier is just a
            name string. Hand-written recursive descent. Parses: types
            (built-in primitives, an identifier type name, a namespace-qualified
            type name `Space:Dir` / `::A:B:T`, + T[] of any); a
            looksLikeQualifiedTypedDecl lookahead routes an identifier-typed decl
            to a var-decl (vs `Space:foo()` / `Space:kX = 1` / `p^ = v` /
            `arr[i] = v`, name-led statements): it scans the (qualified) name, an
            OPTIONAL `^` (reference) or empty `[]` (iterator) suffix, then requires
            the var name — so `Space:Dir x`, `Integer^ ref`, `Integer[] iter`,
            `Space:Dir^ d` all parse (a non-empty `[i]` is a subscript, not a
            suffix, so `arr[i] = v` stays a store; a bare `a ^ b` reads as a
            reference decl `a^ b` since a bare XOR is not a statement form). An
            array DIM (`Int nums[4]`) goes after the name -> the plain `Ident
            Ident` path; a bracket may hold a COMMA dim-list (`int g[3,5]`, the
            natural-order form) — each bracket's dims are appended REVERSED
            (`[a,..,z]` -> `[z]...[a]`), the same transpose parseSubscript applies
            to a `g[x,y]` read/store. A literal dim is validated for positivity
            here (a zero/negative literal -> "Array size must be a positive integer
            constant"); a const-EXPRESSION dim is validated in constfold;
            `alias Name = type;` + bare `alias Ns;` decls; namespace decls
            (`Name { members }`) and inline qualified member decls
            (`const int Space:kSix = 6;`); enum decls
            (`enum [type] [Name] ( m1 [= v], ... )`); function defs/decls with typed
            param lists; var-decls with optional leading `const` (file
            scope or function scope) — incl. a TYPELESS const (`const name =
            expr`, detected by `=` immediately after the name, parseType skipped
            so constfold infers the type); statements (var-decl incl. the
            `<ident> <ident>` typed-decl shape and a `<--` move-init form
            (`T x <-- y`, the move_init flag — `<-->` swap is not a decl), assign,
            aug-assign, move (`a <-- b`) / swap (`a <--> b`) — finishMoveSwap on
            both the bare-name and indexed/deref-lvalue paths, lhs as an expr child,
            alias,
            namespace decl, 0/1/N-arg call possibly qualified, bare inc/dec,
            return, if/else, while + post-condition do-while, the long-form for,
            the RANGED for, and the ENUM for — a qualified name leading a statement
            routes through one parseNameLedStmt); each for varlist head is parsed
            by parseForVarHead as `[type] name` — the var TYPE is optional, and a
            TYPELESS var (the lead is neither a primitive type-start nor a qualified
            typed-decl shape) is inferred / reused at resolve. The for forms then
            dispatch on what follows the `[type] var`: ':' then an operand, then
            '..' -> ranged form (`for (var : start .. [cmp] end [op step]) {body}`,
            cmp `< <= > >= !=` default `<`, op `+ - * / << >>` default `+1`,
            operands are unary-expressions; `==` and `% & | ^ && || ^^` are
            rejected here as invalid range comparator / step operators), emitted as
            a kForRangedStmt that survives to desugar (which lowers it to a
            kForLongStmt, minting `_$end`/`_$step`; the `..` token rides along for
            the empty-range check); ':' then a bare identifier -> ENUM form (`for
            (var : Enum) {body}`) parsed as a kForEnumStmt (resolve dispatches it,
            below);
            anything else is the long form's varlist. parseSwitchStmt parses
            `switch (value) { (case const-expr | default) : stmts ... }` into a
            kSwitchStmt (children[0]=scrutinee, [1..]=kCaseClause, label null =
            default); the value is required and each clause body is an implicit
            block. A case label is parsed under the `case_label_` flag so a
            qualified enum-member label (`case Dir:N:`) resolves its trailing `:`
            as the terminator, not a qualifier (parseQualifiedNameCaseLabel scans
            the maximal `:`-chain and rewinds one segment when the terminator is
            missing). A loop carries an optional `:label` (parseOptionalLabel)
            right after its body `}` — for a do-while between the body and the
            `(cond)`, elsewhere with a required trailing `;`; labels are on loops
            only (a `:name` after a switch is a parse error). break / continue take
            an optional argument — an integer (stored in `text`, the Nth loop) or a
            name (in `name` — an identifier, or the `for`/`while` keyword default).
            expressions
            across the full C precedence ladder (literals + ident, unary
            `! ~ + -`, the `<Type^>` pointer cast (a prefix unary — a leading
            `<` is unambiguous since binary `<` only sits between operands; the
            target spelling rides on return_type, the operand is another unary so
            casts chain right-to-left), the `(Type = expr)` value conversion
            (kConvertExpr — a type-keyword right after `(` opens it, unambiguous
            since no parenthesized expr or tuple starts with a type; parseConvertChain
            parses the target onto return_type and recurses for chain links
            `(A = B = expr)`, right-to-left, no inner parens), prefix/postfix ++/--, full binary set
            arith/bitwise/shift/comparison/logical, parens, postfix-call on
            a bare ident, and the `##` stringify macros in parsePrimary's
            kHashHash arm — ##file / ##line / ##func / ##name(x) (raw lexed
            token text, scanned to the matching ')' by bracket depth) /
            ##date / ##time / ##type(x) (a kStringifyType node, child = the
            operand, lowered to a kStringLiteral in classify), sizeof(...) (a
            kSizeofExpr — a type operand [paren content starts with a type keyword]
            rides on return_type, else the operand is parsed as an expression for
            resolve to dispatch on; lowered to an intptr literal in constfold or
            classify), and new(...) (a kNewExpr — `new`, an optional `(addr)`
            placement prefix [today a `(` always opens a placement address; no type
            spelling starts with `(` yet — a TODO seam marks where the tuple /
            const-pointer lookahead goes], the element type [parseAllocElementType:
            a type WITHOUT a trailing `[`, which is the array size], an optional
            `[n]` size; children[0]=size-or-null, [1]=addr-or-null)). `delete p;` is
            a statement (kDeleteStmt). Stamps (file_id, tok)
            on every node for source
            attribution. No identifier resolution, no scope tracking,
            no type inference, no literal folding — all deferred to
            later stages. Errors are single-shot ("expected '...'") with
            caret at the offending token; sets fatal + early-returns
            up the call chain.
  resolve   parse tree -> annotated parse tree. Builds the symbol table
            (parse::Entry vector on parse::Tree) and resolves every
            identifier-use to a resolved_entry_id. Pushes/pops frames at
            scope-opening nodes (program, function-body today; block /
            class as Phase 2+ land). Pass 1a collects
            program-scope entries (Functions + Consts) without walking
            init expressions; pass 1b-enum + pass 1b walk file-scope enum
            member inits then const init rhs (so globals can reference each
            other regardless of decl order); pass 2 walks function bodies. Owns type aliases: a
            pass-1a-alias pre-sweep registers file-scope `alias` decls as
            kAlias entries; resolveTypeSpelling substitutes an alias chain to
            its underlying (cycle-detected), and resolveDeclType rewrites every
            declared / return / parameter spelling in place before validating
            it (widen::isKnownType) — so downstream stages see only underlying
            types and aliases never reach the ast. requireKnownType also rejects
            `void` with an iterator/array suffix (`void[]`, `void[N]`): void has
            no stride, so a void pointer must be a reference (`void^`). A
            kCastExpr's target type runs through the same resolveDeclType, so a
            cast inherits alias substitution and the void/unknown-type checks; a
            kConvertExpr's target rides the same path (operand resolved as a read,
            target alias-resolved + seg-tok carets). Whenever resolveDeclType erases
            a NAMED type to a different underlying (at a local-var decl site or the
            param pass-1 site), the as-declared alias/enum spelling is stashed in a
            parallel alias_label channel (parse::Entry.alias_label) for ##type to
            report — slids_type stays the erased underlying, so codegen never sees
            a name. A namespace-qualified type
            spelling (`Space:Dir`) resolves via resolveQualifiedType (the lead
            segments walk the shared ns chain, the leaf must be a type) before
            any downstream stage; the cycle-vs-resolution-failure suppression
            flag is named `reported`. A bad segment carets the OFFENDING SEGMENT
            (not the whole type position): parseType captures per-segment tokens
            onto Node.return_type_seg_toks, threaded as a defaulted seg_toks param
            through resolveDeclType -> resolveTypeSpelling -> resolveQualifiedType
            and wired at the var-decl declared-type + cast-target sites (a
            flat-tok fallback covers the sites that don't pass it yet).
            Owns the `##type` operand dispatch: the kStringifyType arm looks up
            the operand (resolveName for a bare name / resolveQualifiedRef for a
            qualified one — both return the entry for ANY kind, erroring only on
            a missing name) and branches on entry kind. A TYPE-NAME operand (a
            kAlias, or an enum's kNamespace type facet) runs through
            resolveTypeSpelling and is stamped on return_type (so `##type(Integer)`
            / `##type(Space:Dir)` -> the underlying); a VALUE operand (kConst /
            kLocalVar) takes the value path; neither is rejected ("'X' is not a
            value or an alias." undefined / "'X' is a <namespace|function>, not a
            value or an alias."). registerEnumMembers also stamps each NAMED-enum
            member's alias_label with the enum name (an anonymous enum has no name
            -> no label -> bare `const int`), so `##type(Enum:member)` reads
            `const Enum`. Integer/char enums auto-increment (C rules); a FLOAT
            underlying (detected STRUCTURALLY via classify(deepStrip), so a float
            ALIAS counts) cannot auto-increment -- a member with no explicit value is
            rejected ("Enum member 'X' of a float type requires an explicit value.").
            The underlying-type "Unknown type" diagnostic carets the TYPE NAME, not the
            `enum` keyword (grammar points the enum node's tok at the underlying token,
            which is the only thing that reads it). The kSizeofExpr arm shares that type-vs-value dispatch: a
            type operand (return_type from grammar) is alias-resolved + validated;
            an ident naming a type stamps the underlying on return_type; any other
            ident / expression is resolved as a value in an UNEVALUATED context
            (sizeof / ##type read only the type — resolveExpr's `unevaluated` flag
            suppresses use-before-init but keeps the read-mark, propagated through
            arith / index / deref operands; no definite-assignment required). kNewExpr
            alias-resolves + validates the element type (resolveDeclType) and
            resolves the size / placement-address sub-expressions as value reads.
            kDeleteStmt resolves its operand as a read (you can't delete an
            uninitialized pointer) and requires it be a variable lvalue — an
            UNRESOLVED ident is left to resolveExpr's own "Unresolved identifier"
            (no cascading "must be a pointer variable"); a resolved non-variable
            (const / function / a non-ident expr) is rejected here.
            Owns namespaces: a kNamespace entry has a persistent frame
            identity that reopens reuse; members ride the enclosing lexical
            lifetime, tagged by owning namespace. Bare lookup walks the open-
            namespace chain (siblings + enclosing namespaces + `alias Ns;`
            imports) then the lexical scope — qualifiers always optional, `::`
            names the global root and only defeats a shadow. Qualified names
            (`A:B:C`, leading `::`) resolve through one shared chain walker
            (refs, inline member decls, bare aliases word identically), each
            diagnostic careting the offending segment. A bare name matching
            members of two different open namespaces / enums is "ambiguous"
            (notes at both decls).
            Owns enums: `enum [type] [Name] ( members )` lowers here (not
            desugar — members must be kConst by constfold). Named -> a
            kNamespace whose slids_type carries the underlying (the name
            doubles as a transparent type alias) + kConst members; anonymous
            -> bare kConst members in the enclosing frame. May be declared at
            file, function, or NAMESPACE scope (registerEnum takes a parent_ns;
            a named enum becomes a member of its enclosing namespace, an
            anonymous one's members do). Values auto-
            increment from 0 (int) / 0.0 (float), C rules; an explicit init
            resets the run. An implicit member is synthesized as
            clone(last-explicit-init) + offset (constfold folds it), so a
            non-literal explicit init like `kB = 1 + 2` continues correctly.
            A file-scope pass-1a-enum REGISTERS names + members (before
            namespaces / aliases); a separate pass-1b-enum RESOLVES the member
            INIT expressions later — after every file-scope entry is collected,
            with the enum's own frame open — so a member init can reference a
            file-scope const (`enum E ( e = kG )`) or a sibling member bare
            (`enum E ( a, b = a )`). A block-scope enum registers + resolves in
            one shot in the body pass (all enclosing entries already exist). A
            namespace-member enum registers in registerNamespaceTree (which
            registers type-introducing members — enums, nested namespaces —
            before consts/functions, so a member's type may name a sibling enum
            regardless of order) and resolves inits in resolveNamespaceBodies.
            Definite assignment + unused locals: the body walk tracks three
            per-function entry-id sets (initialized_locals, read_locals,
            body_locals; all id-keyed, no names). A kLocalVar read before it is
            written → "Use of uninitialized variable 'x'." (params are seeded
            initialized; a decl-with-init or assignment marks written; rhs
            resolves before the mark so `x = x` fires). INFERRED-INIT promotion:
            an assign to a truly-undeclared name (!isQualified && resolveName < 0,
            so a reassign or a wrong-kind target falls through to the normal assign
            path) creates a fresh kLocalVar with empty slids_type, rewrites the
            kAssignStmt to a kVarDeclStmt, resolves the rhs (so `x = x` reads x
            uninitialized), and marks it initialized — the type is left for classify
            to infer + write back. A TYPELESS const (`const name = expr`, empty
            declared type) skips resolveDeclType in both the function-body const
            pre-pass and the main kVarDeclStmt arm (constfold infers the type).
            An end-of-body sweep
            then reports any body-declared local never read: "Unused local
            variable 'x'." if never written, else "Local variable 'x' set but
            never used."; gated on hasErrors so a use-before-init or dup
            diagnostic isn't trailed by a spurious unused report. ARRAYS use a
            separate MAY-set (assigned_arrays): a fixed-size array can't be fully
            initialized in one statement (no initializer lists) and a fill loop's
            element writes wouldn't survive the must-set's loop join, so an array
            read requires only that SOME earlier subscript write exists (monotonic,
            never rolled back) — reading before ANY write still errors; a `^arr[i]`
            address-of marks it assigned, and an iterator-base store (`it[i]=v`)
            READS the iterator (the pointer is dereferenced), not writes it. Consts and
            params are exempt (consts substituted away; params not in
            body_locals). Control-flow joins are modeled by a Completion
            { Normal, Abrupt } that resolveStmt RETURNS: return / break / continue
            are Abrupt, everything else Normal.
            resolveStmtList threads it over a statement list — a statement after
            an Abrupt sibling is "Unreachable statement." (2A) and the dead tail
            is skipped (it declares no locals); both the function body and every
            block walk through resolveStmtList. A kBlockStmt `{ stmts }` opens
            a nested frame: initialized_locals + read_locals FLOW THROUGH (scoped,
            not isolated — an assign/read inside a block affects the enclosing
            local), only body_locals is save/restored so the unused sweep
            (shared sweepUnusedLocals) runs per-block at block exit; a block is
            Abrupt iff its statement list is. Shadowing is allowed (inner masks
            outer via innermost-first lookup, restored on pop). A kIfStmt joins
            definite-assignment at the merge: snapshot the init-set S, resolve
            each arm from S, intersect the arms' out-sets — an Abrupt arm
            contributes the universal set (dropped from the ∩), a missing else
            contributes S unchanged (so an else-less if never adds an init); the
            if is Abrupt iff it has an else and both arms are. read_locals never
            joins (monotonic union — a read on any path is a use). Trailing-return
            correctness (classify) recurses into a trailing block and a trailing
            if/else whose arms both return (endsInReturn / endsInReturnNode).
            Loops: a kWhileStmt (pre-condition) is possibly-zero — condition + body
            resolve from S and the post-loop set is S again (body inits don't
            escape); normally Normal. EXCEPTION (3B revisited): a syntactically-
            constant-true condition (a bool/int/uint/char literal, incl. the
            synthesized empty `()`; condIsConstTrue) with no break targeting the
            loop (loop-frame break_seen false) is NON-COMPLETING — it returns Abrupt
            (so 2A flags its after-code unreachable) and sets Node.non_completing
            (classify reads it for return-correctness, codegen for the `unreachable`
            exit). A named-const-true condition isn't caught (resolve predates
            constfold). A kDoWhileStmt (post-condition, `while { body } (cond);`)
            runs the body once so its inits DO escape: resolve the body from S,
            check the condition's reads against body-out ∩ continue_accum, and set
            after = that ∩ break_accum (the same constant-true non-completing rule
            applies).
            break / continue are Abrupt; each resolves a TARGET frame by its
            argument — NAMED (`break name;` → nearest loop whose label matches; the
            label is the explicit `:name` or the keyword default for/while; switches
            carry none; innermost wins, shadowing allowed), NUMBERED (`break N;` →
            the Nth enclosing LOOP outward, SKIPPING switch frames; N>=1), or naked
            (break → nearest loop OR switch; continue → nearest loop, switches
            transparent) — and folds the current init-set into THAT frame's
            break/continue accumulator (∩, top-seeded via a `seen` flag; a do-while
            / for consume them, a pre-condition while ignores them). It stamps
            Node.loop_levels = hops outward to the target for codegen. NO flavor of
            break/continue is allowed directly in a for-update clause (the
            in_for_update + for_update_floor guard fires first). Errors: count <1 /
            exceeds nesting (caret on the count literal), no enclosing loop labeled
            <name>, inside-a-loop[-or-switch]. A kForLongStmt
            (long-form `for (varlist) (cond) {update} {body}`; the canonical for
            node — other for shapes desugar to it) opens ONE for-scope holding the
            varlist, with the update and body as sibling nested blocks (3 frames;
            the body may shadow a for-var). A TYPELESS varlist decl (empty
            return_type) is intercepted: WITH an init it becomes a kAssignStmt
            (reuse an enclosing local, else fresh inferred-init); with NO init it
            reuses an in-scope local as a no-op slot, errors "Cannot use <kind>
            '<x>' as a loop variable." if the name resolves to a non-local, or
            "Cannot infer the type of '<x>'; it has no initializer." (+ placeholder
            entry to stop a read cascade) if undeclared. Resolved body-then-update
            (execution order): cond reads from the post-varlist set S'; the body
            resolves from S' (break/continue target the for); the update is checked
            against body-out ∩ continue_accum (so it sees body-assigned vars but not
            ones a continue skipped). after = S' (the varlist inits run once
            unconditionally, so they ESCAPE — a reused enclosing var is observable
            after the loop; the possibly-zero body/update inits do not escape). The
            update may not
            break / continue / return — resolved under in_for_update +
            for_update_floor: a break/continue at the update's own loop-depth or
            any return in the update errors ("A '<kw>' statement is not allowed in
            a for-loop update clause."), while a loop nested in the update gives
            its own legal break/continue target. A kForEnumStmt (`for (var : iter)
            {body}`) is the colon-form CARRIER: resolve DISPATCHES on the iterable —
            an array/tuple local (or a tuple literal / `ref^` / call) is UNDERSTOOD
            here (understandForArray / understandForTuple register the loop var,
            resolve the body + DA, validate) and RETAGGED kForArrayStmt /
            kForTupleStmt for desugar to lower; a real enum (a kNamespace with an
            underlying type) is rebuilt as a kForRangedStmt over the enum's
            first..last DEFINED members — find the first/last kConst members by id
            (definition) order in its ns_frame, build `for (var : Enum:first .. <=
            Enum:last) {body}` (members referenced by qualified name → normal
            resolve/constfold fills their values), tagged range-derived so a
            descending/empty enum (first > last) trips the empty-range "Invalid
            range." check on the enum name; then resolved through the RANGED path
            (lowered in desugar, so its `_$end` is minted fresh — no clobber across
            nested typeless enum loops). A kSwitchStmt resolves
            the scrutinee, then each clause body from the entry set S (any case can
            be matched directly, so direct entry is the weakest join input) under a
            loop_stack frame with is_switch=true: naked break targets the nearest
            loop OR switch, naked continue skips switch frames to the nearest loop
            ("A 'break' statement must be inside a loop or switch."). The after-set
            is the ∩ over exit paths (each break point's init-set, the bottom-fall,
            and — default-less — the no-match path = S); a switch with a default and
            no normal exit is Abrupt. An empty
            condition (`if ()` / `while ()` / `while {} ()` / for's `()`) is the
            always-true literal grammar synthesizes via the shared
            parseParenCondition (a slids convention "empty = true"). The loop-frame
            stack (Tree::loop_stack) is transient resolve state, id-keyed like the DA sets.
            Caches lvalue type on AugAssignStmts (s.return_type) and
            return type + param_types on CallStmts/CallExprs (one shared
            resolveUserCall) so downstream stages don't have to re-walk the
            entry table. Sharp diagnostics at the source: wrong-kind entry
            (assign / call use allowlists: only a local var is assignable,
            only a function is callable — every other kind reports
            type/constant/namespace/function and never slips through),
            duplicate decls, return-type mismatch, parameter-type mismatch,
            duplicate definition, arity mismatch, multi-arg print intrinsic,
            print intrinsic used as an expression, needs-qualifier /
            not-visible-from-scope / has-no-member / is-not-a-namespace for
            namespace access, and (final pass) any function declared but
            never defined — anywhere, used or not. Multi-source notes point
            at prior decls. Owns
            the "what does this name refer to" decision; types are not
            resolve's job.
  constfold parse tree -> parse tree. Iterative post-order walker.
            Assigns nominal_type to every literal per fold.sl:16-23
            (bool=uint1, char=uint8, integer/unsigned by smallest-bit-
            tier, float by float32-round-trip). Folds unary on literal
            (rules 1a-1f; `~` is WIDTH-PRESERVING — complements within the
            operand's kind, keeping the kind incl. bool, masking to a strong
            operand's declared width or a weak literal's 64-bit computation
            width) and binary on two literals across all op families: int
            arith / bitwise (signed int64 with rule-6 overflow-to-uint64),
            int shifts (count >= width → 0; uint64 reinterpret to avoid UB),
            int comparisons (int64 path with uint64 fallback), float arith /
            shift / comparison (double + %.17g canonical text; pow2 mul/div
            lowering for float shifts). Substitutes kIdentExpr -> literal node in place
            when the resolved entry is a kConst with a captured value.
            Captures const-decl values back onto kConst entries when
            the rhs folds to a single literal — floats round through
            the declared type for precision capture (3.14 -> float32
            stored as 3.1400001049...); ints/bools/chars store rhs text
            verbatim with range validation against declared type.
            Const KIND/value model (STRONG-truncate / WEAK-widen, so a folded
            const == the same expression on VARIABLES): with ANY strong operand
            (char/bool/intptr or a typed const) emitIntResult flexes a weak literal
            into the strong partner, takes the shared widen::commonType (TypeRef),
            and TRUNCATES the value to the result width (register semantics, no
            demotion-to-flex; emitShiftResult truncates to the lhs width). Two WEAK
            operands keep the old no-width matrix (baseConstKind/promoteConstKind +
            value-widening). A strong no-common-type (int64+uint64, a >64-bit sign
            mix) is REPORTED ("No common type..."), not silently folded. Full model:
            const_truncate_model memory. strong_type on nodes / const_strong_type on
            entries carry strength; trySubstituteConst
            stamps strong_type onto a substituted literal from entry.const_strong_type
            AND carries entry.alias_label onto it (so a const's type label survives
            substitution — needed for the inferred-var case);
            tryCaptureConst infers a typeless const's type (a strong rhs takes its
            type, a bare-literal rhs is WEAK -> weakDefaultType preferred spelling
            with the narrowest nominal kept under the hood) and marks explicit-typed
            consts strong, and stamps the captured value's literal_kind from the
            DECLARED type via literalKindForType (char -> kCharLiteral, bool ->
            kBoolLiteral, float* -> kFloatLiteral, uint* -> kUintLiteral, else
            kIntLiteral) rather than the folded initializer's kind — so char/bool
            consts and `enum char`/`enum bool` members keep their declared kind.
            The capture range-check says "inferred type" for a
            typeless const, "declared type" for an explicit one. walk() returns
            early on a kStringifyType node so the ##type operand subtree is
            fold-EXEMPT (a const under ##type is not substituted to a literal before
            classify reports it). A kSizeofExpr is likewise operand-exempt and
            folds in place (tryFoldSizeof): a statically-known operand — a type
            (return_type), a string literal (length + null), nullptr, an address-of
            (always 8), or a plain ident (its declared type, via
            widen::typeByteSize) — becomes a STRONG `intptr` literal HERE, before
            const capture, so sizeof can initialize a const / feed a const
            expression. Operands needing inference (deref / index / arithmetic) and
            a slid type are left for classify.
            A `(Type = expr)` conversion (tryFoldConvert) with a LITERAL operand
            folds the same way: it computes the C-semantics result (float->int
            trunc, int->int low-N-bit reinterpret, ->bool nonzero, ->float convert)
            and emits a STRONG target-typed literal, so a conversion can initialize
            a const or size an array dimension. A non-literal / pointer operand is
            left for codegen (no compile-time pointer value).
            Iterates to a fixpoint; any kConst whose rhs never folded
            errors with "Initializer for 'X' is not a constant
            expression." (consolidated into one diagnostic with notes
            for additional affected entries when a cycle spans several
            consts). Rejects div/mod by literal zero,
            `~float`, float `& | ^`, shift with float rhs or negative
            count, constants whose value doesn't fit declared type.
            Algebraic identities (x+0 -> x) are NOT done here -- they emit
            a non-literal, breaking the literal->literal contract; scalar
            cases are LLVM's job (see optimize).
  classify  parse tree -> annotated parse tree. Type inference and
            (Phase 3) overload resolution. Reads resolved_entry_id + entry
            data stamped by resolve; never builds entries or pushes frames
            itself, with ONE deliberate symbol-table exception: the inferred-init
            write-back (below). Infers every expression's inferred_type and every
            binary's op_type (computational type). INFERRED-INIT write-back: a
            kVarDeclStmt with empty return_type + !is_const + resolved_entry_id >= 0
            infers the rhs, NORMALIZES a literal-inferred type to its preferred
            spelling (preferredSpelling: int32->int, uint32->uint, float32->float;
            a typed rhs keeps its spelling), copies rhs.alias_label, and WRITES BACK
            entry.slids_type (assert-guarded on hasErrors || rhs.inferred_type
            non-empty); gated to !is_const so it doesn't clobber constfold's const
            inference. A kAugAssignStmt on such a var RE-READS entryType into
            s.return_type (resolve cached it empty before the decl was stamped).
            sizeof lowering: the kSizeofExpr cases constfold left (a deref / index /
            arithmetic operand, or a slid type) become a kIntLiteral of type
            `intptr` — widen::typeByteSize of the type/value operand, or content+1
            for a string literal. A CLASS operand (typeByteSize -1 but a registered
            class) is the exception: its size is the real struct layout LLVM owns, so
            it rewrites to a CALL to `<Name>__$sizeof()` (a runtime intptr, NOT
            foldable — can't init a const). void / an unregistered slid still reports
            "Cannot take sizeof of 'X'."
            ##type(x) lowering: a kStringifyType node becomes a kStringLiteral whose
            text is the operand's resolved type — alias_label ?: inferred_type, plus
            the const qualifier (a kIdentExpr operand resolving to a kConst -> "const
            " + (alias_label ?: slids_type); a const read inside a larger expression
            strips const). The lowering SHORT-CIRCUITS when resolve already stamped
            return_type (a bare/qualified TYPE-NAME operand): it emits that
            underlying spelling and skips inferExpr. alias_label propagation: an ident reads its entry's label;
            unary/shift pass it through; arith uses a sticky binaryLabel rule
            (alias+same-alias or alias+const-literal keeps the label, any mismatch
            drops it); a comparison clears it — inferred_type/op_type/slids_type stay
            the erased underlying so widen/codegen are untouched. Sharp rejections at
            the source: non-coercible operands for ! && || ^^, an if / while / for
            condition not coercible to bool, non-numeric shift sides, bitwise on
            float, no-common-type binaries. Return-correctness (endsInReturn) recurses
            into a trailing block and a trailing if/else whose arms both return.
            Constant-condition unreachable detection (runs HERE, post-constfold, so
            a folded literal / substituted const / synthesized empty-`()` is
            visible — vs resolve's 2A which is pre-constfold): constTruth folds the
            condition to True/False/NotConst; a const-true if flags its else dead,
            a const-false if flags its then, a const-false while flags its body —
            "Unreachable statement." at the dead branch's first statement (empty
            branch = nothing to flag). The const-TRUE-LOOP unreachable-after case
            (3B revisited) is handled in RESOLVE (a non-completing loop returns
            Abrupt, so 2A flags the code after it), not here; classify's only
            const-true-loop role is endsInReturnNode reading Node.non_completing for
            return-correctness. A kForLongStmt
            classifies its loop var (children[3]) FIRST so a typeless one is typed
            before the rest; for a ranged/enum for (range_dotdot_tok set) the
            loop-var type is then stamped onto any typeless `_$end`/`_$step`
            (children[4..]) so their bounds flex into it — matching an
            explicitly-typed range. Empty-range check (ranged-for only, gated on
            range_dotdot_tok): if a ranged-for's start and end both fold to literals
            and `start cmp end` is false, the body can never run -> "Invalid range."
            caret on the `..` (rangeFirstTestFalse compares the two literals; no
            infinite-loop check — deferred, todo.txt). A kSwitchStmt checks the
            scrutinee is integer-class (float rejected); each case label must be an
            integer constant (constfold folded it) that FITS the scrutinee type
            (literalFitsContext — an out-of-range / sign-mismatched label is
            rejected, never emitted as a truncated `iN`) and is unique by value
            (full 64-bit dedup, so 'a'==97 and 1+2==3 collide, with a "first case
            here" note); default is singular. A switch is a return-terminator
            (endsInReturnNode) iff it has a default, no clause has an escaping break
            (containsBreak — the same test codegen uses), and the LAST clause's
            body ends in a return; C-style fall-through carries a stacked empty /
            non-returning clause into that final return.
            Per-arg type inference at call sites uses the resolved
            callee's param_types (cached on the kCallStmt/kCallExpr by
            resolve) as context. A kCallExpr's inferred_type is the
            callee's return type; a void return used as a value is rejected
            here. Pointer-cast rules (Phase 4) live here as two predicates:
            ptrImplicitOk (a bare assignment may only STRIP info — nullptr->any,
            any->`void^`/`intptr`, iterator->reference of the same pointee) and
            ptrExplicitOk (a `<Type^>` cast additionally bridges through a
            buffer-class pointer [`void^`/`int8^`/`uint8^`] or `intptr`, and
            reinterprets iterator<->reference of the same pointee; two unrelated
            non-buffer pointers must chain through `void^`; only `intptr` bridges
            pointer and integer). checkPtrAssign enforces the implicit rule at
            EVERY pointer-assignment site — kVarDeclStmt init, kAssignStmt, and
            kStoreStmt (a references-array element / deref slot) — gated on a
            pointer being involved (pure-numeric assignments keep the width path).
            The kCastExpr arm validates the explicit rule and stamps the target.
            The kConvertExpr arm (`(Type = expr)`, Phase 4) infers the operand
            with NO context (it retypes, never flexes) then gates the grid: the
            target must be a value type (a pointer/iterator -> "may not be a
            pointer type", anything else non-numeric -> "must be a value type");
            a value source converts to any value target; a pointer/iterator source
            converts ONLY to `bool` or `intptr` (else "a pointer converts only to
            'bool' or 'intptr'"); a non-value source is rejected outright. It
            stamps the target as inferred_type (even on the error paths). Because
            the result is a strong typed value, an over-narrow assignment of it is
            caught by the same checkStrongConstAssign as any typed value.
            Move / swap (Phase 4): kMoveStmt classifies like a store — infer the
            lhs lvalue, the rhs in that context, then checkPtrAssign +
            checkStrongConstAssign (so a move COPIES under assignment rules; a
            narrowing move rejects). kSwapStmt requires the two operands' deepStrip
            types be IDENTICAL (no widening — a symmetric exchange can't convert
            both ways), else "Swap operands must be the same type". resolve owns
            the lvalue rule: resolveMoveSwapLvalue rejects a non-lvalue (a swap rhs
            is a general expression, so `x <--> 7` would otherwise crash codegen)
            — BOTH swap operands and a move's LHS must be lvalues ("A swap operand"
            / "A move target" must be an lvalue); a move's RHS is a plain read, so
            an rvalue source is allowed. DA: a move lhs is a pure write (need not
            be pre-init); a swap reads+writes both (both must be init).
            kNewExpr: a heap element must be sized — a primitive (compile-time
            typeByteSize) OR a CLASS (runtime __$sizeof, so it IS allocatable); void
            / unsized -> "Cannot allocate" (carets the element type, name_tok). An
            array size must be integer-class; a placement address must be a
            buffer-class pointer (isBufferClassPtr, the cast set void^/int8^/uint8^);
            `new T(args)` ctor args (children[2]) belong to a single class (rejected
            on a primitive / array). The result type is element + (array ? "[]" :
            "^"). kDeleteStmt's operand must be a pointer type. See the feature
            section "CLASSES: NEW / DELETE / SIZEOF" for the construct/destruct
            lowering. Future: overload resolution when multiple Function entries
            share a name.
  desugar   parse tree -> ast (separate node-type set). Today: identity
            copy that propagates every annotation classify and constfold
            stamped (nominal_type, inferred_type, op_type, resolved_entry_id,
            params, param_types, file_id, tok). Two rewrites are live.
            (1) aug-assign (`lhs op= rhs`) -> `lhs = lhs op rhs`, with the
            synthesized IdentExpr + BinaryExpr inheriting the aug-assign's
            classify-stamped types so codegen sees the rewrite as if it
            were classified directly. (2) PPID: a post-copy pass extracts
            ++/-- per phrase, replacing each with a read; also drops parse-
            only nodes (alias, namespace) and hoists namespace member
            functions to program scope with entry-id-derived symbols (no
            cached canonical-name strings). Statement-level
            bumps splice as sibling kExprStmt bump-statements around the
            statement (post AFTER the store -- the statement is the phrase);
            a bump inside a sub-phrase (call args, && / || rhs) stays in a
            synthesized kSeqExpr {pre... value post...} so a short-circuited
            bump never fires. The statement-bump splice (lowerStatementList)
            recurses into a kBlockStmt, a kIfStmt (lowerIfStmt: the condition is a
            self-contained phrase whose bumps fire before the branch, and the arms
            recurse), a kWhileStmt / kDoWhileStmt (lowerWhileStmt: the condition is
            a phrase re-tested each iteration, the body recurses), and a
            kForLongStmt (lowerForLong: varlist initializers + condition are
            phrases, the update + body are statement lists) so a bump inside them
            splices within that scope, not at function scope. The OTHER for-shapes
            now lower HERE too (no longer in grammar/resolve): lowerForRanged /
            lowerForArray / lowerForTuple build a kForLongStmt subtree, minting
            helper locals (`_$idx`/`_$end`/`_$step`/`_$iter`/`_$ftmp`) with fresh
            resolved_entry_ids from one program-wide counter seeded at entries.size()
            — resolve/classify UNDERSTAND the short form, desugar lowers it (no
            parse-tree mutation; codegen is node-driven). The for-tuple iterator is
            `<T[]><void^>base` (the void^ bridge), so a `ref^` deref dodges
            addr-of-through-deref. Future rewrites (receiver shapes, more operator
            dispatch) slot in as their phases land.
  optimize  ast -> ast in place. Slids-aware perf rewrites LLVM can't do
            (compound-fuse, NRVO, identity-temp adoption, build-into-target).
            (TODO stub.)
            (No layout stage: every job the original plan gave it lands in a
            stage with the right inputs — field offsets = codegen index-GEP, struct
            sizes = the GEP-null/ptrtoint `__$sizeof` helper, mangled names =
            desugar, needs_ctor/dtor = resolve, vtables [Phase 7] = resolve slot
            assignment + codegen emission. An ast-only pass has neither the entry
            table nor the class graph, so it can't own any of them.)
  codegen   ast -> .ll text. Reads inferred_type / op_type stamped by
            classify (no longer derives or recomputes types). SymTab keyed
            by parse::Tree::entries index; every ident / lvalue node carries
            its resolved_entry_id, so codegen does no string-keyed lookup.
            Function definitions emit param allocas + stores from %arg.N
            into named registers; calls emit `call <ret> @name(<typed
            args>)` via one shared emitCall using the classify-stamped
            return_type and param_types — the statement form discards the
            result register, the expression form (kCallExpr) widens it into
            the destination type. ALL local allocas are HOISTED to the function
            entry block (emitFunction pre-walks the body via collectVarDecls): an
            alloca emitted at its declaration site would re-allocate stack on
            every pass through an enclosing loop — unbounded growth → stack
            overflow (a v1 bug); the entry block runs once. Each uses the register
            name `%<name>.<entry_id>` — the entry-id suffix is the SHADOWING-SAFETY
            mechanism: a shadowing inner-scope local (`int x; { int x; }`) gets a
            distinct register from the outer `%x`, so llc doesn't reject a
            duplicate local value name. The id-keyed SymTab resolves each read to
            the right entry, so the suffix is a deterministic local derivation,
            NOT a cached canonical name (writer and reader agree by entry id; the
            string is never stored or re-derived elsewhere); the kVarDeclStmt arm
            then emits only the initializer store. Logical && / || / ^^
            (emitLogical) lower to PHI nodes, NOT alloca — an alloca there would
            land in a loop-header block when the logical is a loop condition and
            leak a slot per iteration (same class); the short-circuit + rhs edges
            route through dedicated known-label blocks so the phi predecessors are
            always valid. A kBlockStmt is transparent (emit children in order). A
            kIfStmt emits emitToBool on the condition + a conditional br to
            then/else/merge labels (no phi — definite-assignment rides the hoisted
            allocas); an arm ending in a control transfer (return / break /
            continue) emits no br-to-merge, and when every arm transfers the merge
            block is omitted entirely (resolve's 2A guarantees nothing live
            follows). A kWhileStmt is head/body/exit (test-first); a kDoWhileStmt
            is body/cond/exit (body-first, test after); a kForLongStmt runs the
            varlist init stores once (allocas hoisted) then head(cond)/body/update
            /exit (continue -> update, so the loop var still advances on continue).
            break -> exit, continue -> head (while) / cond (do-while) / update
            (for), via a LoopCtx { header, exit, outer } LINKED LIST threaded
            through emitStmt; kBreakStmt / kContinueStmt walk Node.loop_levels
            `outer` hops (resolve-stamped, asserted in range) to the target frame
            and branch to its exit (break) / header (continue) — so a labeled /
            numbered break reaches a non-innermost loop. The body's
            back-edge is emitted only if it can fall through (endsTerminated).
            A kSwitchStmt lowers to an `llvm switch` on the scrutinee dispatching
            to one block per clause (source order, default's block = the switch
            instr default, or exit when there is no default); each block emits its
            body then falls through via br to the next clause (or exit) unless
            terminated, so C-style fall-through is the natural block layout. A
            clause's LoopCtx inherits the enclosing loop's header (continue passes
            through) but overrides exit = the switch exit (naked break). The exit
            block gets an `unreachable` terminator only when nothing reaches it
            (no escaping break via containsBreak, no bottom-fall, has a default).
            endsTerminated / endsTerminatedNode (return + break + continue, and a
            block / both-armed-if whose paths all do) drive the if-arm and loop-
            back br decisions; a loop is never terminating (it reaches its exit).
            emitFunction's trailing-return terminator decision uses a separate
            ast-side endsInReturn / endsInReturnNode (return only) that recurses
            into a trailing block and a both-arms-return if, mirroring classify.
            Mangled names are baked in by desugar (functionSymbol); field offsets
            are the index-GEP here in codegen (LLVM owns the byte offset).
              Pointers & arrays (Phase 4). References (`T^`) and iterators
            (`T[]`) are both LLVM `ptr`; `anyptr` (nullptr) too. kAddrOfExpr
            `^var` is the operand's alloca register (no load); kDerefExpr loads
            the pointer then loads the pointee. A fixed-size array `T name[N]`
            is an aggregate alloca, STANDARD row-major (`int[5][3]` ->
            `[5 x [3 x i32]]`, leftmost dim outermost — llvmForRef wraps the dims
            last-first); emitElementAddr walks a kIndexExpr chain to the base and
            emits ONE getelementptr with the indices in SOURCE order (leftmost
            subscript outermost — it iterates the chain in reverse to do so) — it
            also rejects a partial index (chain length must equal the dimension
            count). The COMMA subscript `a[x,y]` is a grammar-level transpose
            (`[a,..,z]` -> `[z]...[a]`, parseSubscript), so by codegen it is just a
            chained kIndexExpr. An ITERATOR
            subscript instead loads the pointer and GEPs by element type.
            kStoreStmt stores through any lvalue expr (deref / index). Move /
            swap (kMoveStmt / kSwapStmt, `a <-- b` / `a <--> b`) are lowered HERE
            (they pass through desugar untouched). A MOVE copies the rhs into the
            lhs via emitExpr(rhs, dest=lhs type)+store (so it reuses the widen /
            implicit-pointer-cast / whole-tuple machinery, exactly an assignment),
            then emitNullLeaves walks the rhs's structured type and `store ptr
            null`s every pointer / iterator leaf — recursing by GEP into nested
            tuple slots (the "fancy case"), so the source is left valid; an rvalue
            source (isAstLvalue false) has no address and is a pure copy. A SWAP
            loads both lvalues into SSA temporaries and stores them crossed (no
            stack temp; a whole-value load/store handles tuples; both loads precede
            either store, so an aliased swap is safe). emitLvalueAddr gives an
            operand's address (a bare var's alloca / emitElementAddr / a deref's
            pointer); a move-init decl (`T x <-- y`) nulls after the var-decl store.
            Iterator
            arithmetic: `iter ± int` is a signed element GEP, `iter - iter` is
            ptrtoint-diff / elemBytes, `++`/`--` GEP ±1 element. Pointer
            comparisons icmp the raw pointers (unsigned). A kCastExpr
            (`<Type^> x`) emits the operand then widen::convert twice
            (operand->target->dest); convert's pointer head makes ptr<->ptr a
            value no-op (opaque `ptr`), ptr->intptr a `ptrtoint`, intptr->ptr an
            `inttoptr` — and asserts if a pointer ever reaches the SCALAR
            conversion path (an ungated ptr->non-intptr would store a `ptr` as an
            integer; classify rejects them, so this guards against a missed gate).
            A kConvertExpr (`(Type = expr)`) mirrors the cast shape: emit the
            operand at its own type, then widen::convertExplicit (operand->target)
            then widen::convert (target->dest). convertExplicit is the FULL value
            grid (sibling to convert, which only widens): trunc / sext / zext /
            fptrunc / fpext / fptosi / fptoui / sitofp / uitofp, a same-width int
            change is a no-op (sign reinterpret is free), `->bool` is a nonzero
            (`icmp`/`fcmp une`) / non-null test, a pointer source is `ptrtoint`
            (->intptr) or the non-null test (->bool). It never reports — classify
            pre-validated — and asserts on any state classify should have caught.
            kNewExpr: `new T` -> `call ptr @malloc(i64 sizeof(T))`; `new T[n]` ->
            `mul i64 n, sizeof(T)` then malloc; placement (children[1]) -> the
            address itself, no allocation (primitives construct nothing). An
            assert guards an unsized element (classify gated it). kDeleteStmt:
            load the pointer, `call void @free(ptr)`, store null back to its
            alloca. malloc / free are declared in the module preamble next to
            printf. No destructors run (Phase 5).

PRODUCT FILES (.h / .cpp pairs)

  token     Token { kind, text, file_id, line, col, length }. List owns the
            token vector AND a per-file registry { path, source, line_starts,
            imported_by }. APIs: add(), openFile(). Deque-backed file table
            so Stream pointers stay stable across imports.
  parse     parse-tree node types + tree storage + build/walk/annotate APIs.
            Nodes carry: nominal_type (literals, populated by constfold),
            inferred_type + op_type (expressions, populated by classify),
            resolved_entry_id (idents / lvalues / callees, populated by
            resolve), name_tok (ident token of named constructs — VarDecl,
            FunctionDef/Decl, Param; populated by grammar so entry.tok
            carets at the ident rather than the const/type keyword),
            is_const (kVarDeclStmt: declared with leading `const`),
            params (kFunctionDef/Decl: ordered kParam children),
            param_types (kCallStmt/kCallExpr: cached resolved-fn param-type
            strings driving each arg's emit dest_type). Owns the symbol table:
            Entry vector + frame stack + pushFrame / popFrame / addEntry /
            findInLiveScopes / findInFrame / entryType APIs that resolve
            calls. Function entries carry param_types alongside their
            return type, plus num_required (optional-param boundary) and a
            def_tok/def_file_id pair (the first DEFINITION's position,
            distinct from tok = the first DECLARATION) so "first defined
            here" and "first declared here" notes caret the right
            occurrence across a forward decl + later body; kConst entries
            carry literal_text + literal_kind
            (filled by constfold; read by constfold at substitution sites).
            Stage-vs-product rule: stages make decisions, parse owns
            storage and lookup.
  ast       ast node types (separate set from parse) + tree storage +
            build/walk/annotate APIs. Nodes carry nominal_type +
            inferred_type + op_type + resolved_entry_id + params +
            param_types propagated from parse; a call/def `name` carries the
            mangled symbol (baked in by desugar's functionSymbol). No
            layout-offset fields — LLVM owns the byte offset (codegen index-GEP).
            file_id / tok back-pointers attribute to source.

PLUMBING

  main.cpp        Pipeline orchestrator. Parses argv (-o, -I), opens the
                  root via lex::run, walks the seven stages, prints
                  diagnostics + exits 1 if any errors, otherwise runs
                  codegen.
  diagnostic.h/.cpp  Record { file_id, tok, message, notes }, Note (same
                     shape), Sink (vector of records). APIs: report(),
                     hasErrors(), render(). render() walks the unified
                     token::List to look up source line/col/length and the
                     file registry for path + context lines + caret-sled +
                     imported-by chain. Caret-sled is bracketed at both
                     ends: length 1 → `^`; length 2 → `^^`; length N → `^---^`
                     (v1-style). Color-gated on isatty + NO_COLOR. Messages
                     are sentence-shaped throughout: capital + period.
  Makefile        -std=c++17 -Wall -Wextra -Werror -Wswitch-enum.
                  Builds to ../bin/slidsc. Objects in ../build/compiler_v2/.
