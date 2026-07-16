compiler — slids compiler rewrite, in-progress

(Companion docs: plan.txt = the phase roadmap / main quest; todo.txt = open
side-quests — bugs, reach goals, deferrals; readme-classes.txt = the CLASS-cluster
per-stage notes, spun out of this file. This file = per-stage current-state
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
    kIterator / kArray / kSlid / kTuple / kAlias / kConst) plus its payload (cat+
    bits, pointee, elem+dims, slots, underlying). spell(intern(s)) == s exactly
    (bar kAlias, which is minted not parsed), guarded by `slidsc --type-selftest`.
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
    get(ref) returns a `Type const&` INTO the arena, which is a `std::deque<Type>`
    (NOT a vector) precisely so a `push_back` on intern never relocates existing
    elements — outstanding refs stay valid across an intern*. TypeRef is an integer
    index, so `operator[]` indexing is unchanged. (Before the deque, a vector realloc
    dangled every held ref, and a capture-before-intern discipline held it together
    per-site; the deque removes that requirement and the whole bug class.)
  * kAlias is a TRANSPARENT type: spells as its name (for ##type/diagnostics) but
    sees through to `underlying` for every structural query (classify / llvm /
    size / known / the form-predicate cluster via strip). Aliases + enum type
    facets are kAlias; alias_label is now a derived display cache, not a channel.
  * kConst is the const qualifier — a facet wrapping `underlying`, like kAlias:
    TRANSPARENT for matching (strip / deepStrip / classify / isKnownType /
    typeByteSize see through it; deepStrip ERASES it, so `(const T)^` == `T^` for
    equality) but VISIBLE in spell() (`const T`, and a const child under a `^`/`[]`/
    `[N]` suffix parenthesizes to `(const T)^`). NO enforcement yet — const is a
    representation-only qualifier (Phase 6 enforces). Placement encodes deep vs
    shallow: an OUTER kConst is `const T^` (the whole pointer + its data are const);
    a kConst on the pointee is `(const T)^` (mutable pointer, const data). intern()
    peels a leading `const ` FIRST so the prefix binds loosest (deep); the
    parenthesized form keeps const inside, so the suffix lands outside (shallow).
    Helpers: internConst (collapses const-const, drops on kNoType), removeConst
    (strips every const, rebuilding wrappers; preserves aliases — the `<mutable>`
    transform), deepConst (the MIRROR — const-qualifies every MUTABLE position:
    array elem, tuple slot, pointer + pointee; the `const`-declared-variable
    transform). leafIsKnownClass / requireKnownType's leaf-walk peel kConst too.
    A `const`-declared LOCAL whose type is a foldable SCALAR is a SUBSTITUTED named
    constant; a non-scalar one (array / tuple / class / pointer / iterator) is a
    not-mutable VARIABLE — resolve's constNeedsStorage gate routes it to a kLocalVar
    with a deepConst type instead of the substitution path (unenforced; behaves as an
    ordinary local). At a NON-RUNTIME scope (file / namespace / class body) a
    non-scalar const still errors "requires global storage" — globals now HAVE storage
    (the compound-lazy path), but the const path isn't routed through it yet (todo.txt). ast
    `is_const` means "substituted constant", set from the entry kind in desugar.
    A TYPELESS const (`const x = rhs`, no declared type) is the SAME split but
    decided in CLASSIFY (the type is unknown at resolve): constfold DEFERS a typeless
    const it can't fold (slids_type still kNoType), then classify infers the rhs type
    and flips a non-scalar one to a kLocalVar + deepConst; a leftover typeless SCALAR
    is the genuine "not a constant expression" (a runtime-scalar const variable is a
    todo).
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
    Unrelated pointees are an error (an explicit cast is required). An ARRAY source
    DECAYS to a pointer: a bare array implicitly casts to the ELEMENT pointer
    `Type[]`/`Type^` — classify rewrites it to `^arr[0]` (checkValueAssign /
    wrapArrayAsElemAddr), so it flows through the normal pointer path. The
    WHOLE-array ref `Type[N]^` is NOT implicit from a bare array (write `^arr`);
    only a function ARGUMENT gets the whole-ref convenience (`fn(arr)`==`fn(^arr)`).
  * accept — terminal. intptr <- pointer/nullptr lowers to ptrtoint; pointer/
    void^ <- nullptr is the null store. void^ <- ANY pointer is the universal
    erase (the `to == void^` arm) — accept-in, cast-out: a void^ SOURCE into a
    typed pointer falls under `ptr` and is rejected.
  * recurse — the aggregate x aggregate block: match slot/element count, then each
    element/slot pair RE-ENTERS this matrix. Self-similar to any depth.
  * op=    — the class row dispatches the class's assignment operator: a matching
    user `op=` (classify rewrites the assign / store / field / deref / index target
    to a method call) or, absent one, the default same-type copy / move; a source with
    no match is an error. LANDED (operator overloading + the declarator dispatch funnel).
    A `class <- tuple` as a ctor is CONSTRUCTION (below), not this cell. A `= (tuple)` value
    that a user op= accepts DOES dispatch it now (2026-07-15): classify::buildClassFromValue
    routes a class-from-a-VALUE at every declarator site — root decl, tuple slot, array
    element, return — through op= (else field-list), and the transfer peel builds it in place;
    `Class c(args)` (parser `construction_init`) stays field-list — and a field-list may carry
    EMPTY slots (`Class c(,2,3)`), each defaulting that field (2026-07-15); the `= (tuple)` form
    may not. See readme-classes.txt.
  * error  — everything else.

  intptr-as-source falls in the integer column, so `pointer <- intptr` is an error
  (implicit); the intptr<->pointer accept is one-directional (intptr <- pointer).
  The ptrtoint lowering is the EXPRESSION's job, not the store's: codegen's
  kAddrOfExpr arm routes its result through widen::convert(inferred_type, dest_type),
  so `^lvalue` into an `intptr` lvalue emits `ptrtoint` (ptr->ptr stays a no-op,
  kNoType is "no conversion"). Before this it returned the raw `ptr` and stored it
  into an i64 slot — invalid IR (`store i64 <ptr>`); canon test/class/empty.sl.

  A SIZE-1 CONSTRUCTION TUPLE collapses BEFORE this matrix. The `Type name(value)`
  form builds an explicit 1-element kTupleExpr (grammar); for a NUMBER or POINTER
  target checkValueAssign unwraps it to its element (the node-level 1-tuple==scalar
  collapse — the `=`-grouping form gets it at parse), so the element then flows
  through the normal integer / float / pointer cell. `int x(42)` == `int x = 42`
  (this is why the `integer <- tup/arr = error` cell does not fire); a 0- or
  2+-element `(args)` on a scalar stays the tuple-vs-scalar mismatch. Without the
  collapse the tuple node reached codegen's scalar fill and segfaulted.

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
    distinct converts) -- NOT a whole-aggregate copy. A single memcpy/aggregate
    store is only the degenerate case where every element is already identical.
    Landed end-to-end, FORM-AGNOSTIC (an array IS a homogeneous tuple):
    classify::checkAggregateShapeMatch matches array and tuple as the SAME shape at
    every level -- slot COUNT (aggregateSlotCount) per level + a per-leaf widen
    (checkValueWiden), so cross-form (`(int[2],int[2]) <-> (int,int)[2]`, any
    nesting) is accepted and a leaf NARROW is rejected at classify (was a codegen
    assert). Every cross-form value copy routes through it; the old count-only
    classifyArray/TupleFrom*Value arms + tupleFlatSlotCount are DELETED. The COPY
    itself is lowered BY SLOT in desugar (lowerAggregateList: a cross-form / leaf-
    widen copy at a decl/assign/store becomes per-leaf kStoreStmts over a form-
    agnostic kIndexExpr chain `dst[i] = src[i]`, recursing; a TUPLE LITERAL source is
    NOT a cross-form copy at all and is skipped — a literal is not an OBJECT to be read
    once and re-indexed, it IS elements, and every binding site already distributes one
    by slot at the destination's element type (emitInitFill's array<->tuple bridge), so
    spilling it would materialize the whole aggregate for nothing;
    a non-lvalue source
    spills to `_$agg`, and a side-effecting index/operand of an LVALUE source is
    hoisted to a `_$ix` temp (hoistLvalueSideEffects) so the source — and a move's
    per-leaf null — is evaluated ONCE, not per slot), so codegen sees only scalar
    leaves there. MOVE and RETURN
    lower by slot in desugar too: a cross-form / leaf-widen MOVE adds a per-leaf
    source null (emitAggNullLeaves, the desugar analogue of codegen::emitNullLeaves);
    a cross-form / leaf-widen RETURN materializes a `_$ret` temp of the return type
    and copies into it by slot, so codegen's kMoveStmt / kReturnStmt see only a
    SAME-type whole-value op. (A non-primitive return is then emitted via sret — see
    NON-PRIMITIVE RETURN below — so kReturnStmt CONSTRUCTS that value into the
    caller's slot rather than returning it by value.) The residual seams desugar does not
    visit (call-arg, const-decl) use the now FORM-AGNOSTIC codegen::emitImplicit-
    AggregateConvert (extractvalue index i is identical for an LLVM array and a struct,
    so one walk converts both forms); the four old flatten HACKS (emitArrayFromTuple-
    Value / emitTupleFromArrayValue / emitTupleLeafStores / emitArrayElemStores) are
    DELETED. The all-identical fast path keeps the whole-aggregate store. Call-site uses classify::
    shapesAndLeavesMatch in argConvertCost to rank shape-match-with-elem-widen as
    a per-leaf widen rung (leafWidenRung; exact still wins overloads). NUMERIC END-STATE:
    classify::checkValueWiden ports widen::convert's reject rules (narrowing /
    cross-family / sign-change) and runs at the classify level (every
    assignment-family site + kAugAssignStmt + kForRangedStmt + kForArrayStmt).
    widen::convert is now pure lowering -- its narrow/convertErr paths ASSERT
    if a classify gate is missing.

NON-PRIMITIVE RETURN — sret + RVO / NRVO (landed; [[project_aggregate_return_roadmap]] step 3)

  A function returning a class / array / tuple is lowered to sret: codegen emits
  `void @fn(ptr %sret.in, <params>)` (isSretReturn = array/tuple/slid), and the body
  CONSTRUCTS its result into the caller-provided slot `%sret.in` and never destructs
  it (the caller owns the dtor). Primitive / pointer returns are unchanged (by value).

  * FUNCTION side (kReturnStmt, when isSretReturn(fn_return_type)):
    - NRVO: a returned local L of the exact return type whose lifetime is DISJOINT
      from every other returned local (and from any slot-writing return) is built
      directly in %sret.in. desugar::analyzeNrvo computes ineligibility by a scope walk
      (a candidate is out if another returned-local is live at its decl, or it is live
      at a slot-writing return — decl-point liveness, so good()/q-after-if/if-else are
      eligible and both bad() shapes are not), then marks each eligible L's decl + its
      `return L` `nrvo`. codegen gives L NO alloca (its storage IS %sret.in), builds it
      in place, does NOT register it for destruction, and `return L` is a bare
      `ret void`. MULTIPLE disjoint locals (good(): a different local per arm) all
      alias %sret.in safely — only one is live per path; overlapping locals (bad())
      fall back to the move. Applies to ANY sret return (analyzeNrvo gates on array/tuple/slid):
      a HOOK return becomes one construct / one destruct (the caller's); a POD
      aggregate / class elides the copy into the slot. The cross-form / leaf-widen
      case rides along too — the `_$ret` temp desugar materializes is itself a single
      exact-typed returned local, so it NRVOs and the by-slot copy writes %sret.in.
    - return-OF-call (`return g()`): forward — emitCall(g, sret_dst=%sret.in), so g
      builds directly into our slot; no temp, no extra ctor.
    - FALLBACK (a non-NRVO named local / rvalue): move-init %sret.in from the value —
      a whole-class source calls the class's move function `@<Class>__$move` (the user
      op<-- if defined, else the synthesized memberwise move + source-null), then the
      ctor runs; a named-local source is left a husk, dtor'd by the scope unwind. A
      defensive assert checks the value is exact-typed (desugar lowers any cross-form /
      leaf-widen return to an exact `_$ret` temp first).
  * CALLER side:
    - new decl, exact type (`Class x = fn()`): BUILD IN PLACE — emitCall(fn,
      sret_dst = the local's alloca); the local is constructed by the callee and
      registered for destruction (Phase-B case 1). ELIDE-WHENEVER-POSSIBLE: this holds
      EVEN when the class defines op= / op<-- (classify's dispatchAssignInit elides the
      op for a same-type class rvalue decl-init); the op fires only for the existing-var
      / lvalue-copy / non-exact cases below.
    - existing POD var, exact: OVERWRITE in place (case 2).
    - existing hook var, or non-exact: temp + assign fallback (case 3) — the `=` form
      calls the target class's copy function `@<Class>__$copy` (user op= or the
      synthesized memberwise copy), the temp then destroyed; the result temp reclaimed via
      stacksave/restore (no per-iteration stack growth).
    - discarded call (`fn();`): build a temp, destroy it (also stacksave-bracketed).
    - INLINE (expression) position (`g(mk())`, a nested call): desugar::liftSretCallList
      hoists the hook-returning call to a `_$cret = call;` temp decl (codegen's
      kVarDeclStmt sret-intercept then owns it), replacing the call with an ident.
      LIFETIME: the lifted temp is folded into a kSeqExpr wrapping the rhs, so it dies at
      the STATEMENT (liftSretCallList, 2026-07-11) — for EVERY rhs value form since
      2026-07-13. It used to be SCALAR-only: a CLASS-valued rhs is built IN PLACE by the
      statement's sret fast paths (kVarDeclStmt sret-intercept / kReturnStmt sret /
      kAssignStmt case 2/3), and those matched on the RAW call node, so a seq around it
      would hide the call and force an extra copy — the wrap was declined and the arg temp
      fell back to enclosing-scope lifetime (evaluate.sl case 7). Codegen now OPENS the seq
      at those three statements (`openRhsSeq` / `closeRhsSeq`, codegen.cpp): it constructs
      the seq's temps, hands the sret path the seq's VALUE child, then destroys them — so
      in-place construction and statement-scoped temps hold AT ONCE, and the form test is
      gone. The seq's effect children are emitted by one shared `emitSeqEffects`, so an
      expression seq and a statement-rhs seq give a temp the same lifetime.
      Positions the lift does NOT cover yet — a store target, a loop / if condition, a
      move/swap operand — are REJECTED at codegen (emitCall, value position) with
      "Returning a class by value in an expression position is not yet supported"
      rather than miscompiled.
  POD-aggregate returns ride the same sret ABI (behavior-neutral vs the old by-value
  return) and NRVO too (eliding the copy). Returning an unnamed temporary (`return Class;`
  / `return Class(7)`) now compiles (was a front-end gap). Canon:
  test/function/return_fn.sl.

ANONYMOUS TUPLES + #x (landed this phase; spans every stage)

  * A tuple is Form::kTuple (slots). LITERAL `(a,b,...)` is kTupleExpr (grammar:
    parsePrimary, comma after the first paren expr; size-1 collapses to the bare
    expr). TYPE `(T,T,...)` parses in parseType, each slot a ListSlot declarator
    (anonymous — a NAMED slot `(int x, int y)` is "too many names"); a `(`-led
    statement disambiguates via looksLikeTupleTypeDecl (a type-suffix run then a
    trailing name — the shared typeSuffixesThenName tail, so `(const int)[5]^ p`
    and `(int,int)[2]^ t` parse) / looksLikeTupleDestructure (`)=`).
  * Landed: construct + whole-copy + const-index read `t[k]` (extractvalue; a
    RUNTIME index on a tuple is rejected — heterogeneous slots); slot write
    `t[k]=v` (struct-GEP) + destructure `(a,b,)=t` (kDestructureStmt, each slot a
    BindSlot declarator; a null child = a skipped slot — a bare `,` OR a typed-no-name
    `int` discard); slot-wise arith + scalar broadcast (`(1,2,3)+7`); references
    (`(T,T)^` = ptr). A non-primitive RETURN goes via sret (see NON-PRIMITIVE
    RETURN), not by value. Codegen builds the aggregate via insertvalue; classify
    slot-types via internTuple.
    A non-primitive VALUE param is now a COMPILE ERROR (mungeParamTypes, resolve) — a
    tuple / class param must be a pointer (`(T,T)^`). The ARRAY SHORTHAND `int a[3]`
    (and `int[] p`) is the one exception: mungeParamType rewrites it to `(const int[3])^`
    (pointer-to-const array unless `mutable`) and classify + codegen implicit-deref it
    at each index (`a[i]`, no `^`), so it passes BY POINTER with no copy. Passing a
    bare ARRAY to a pointer param is no longer a special case — it is the general
    array->pointer decay (see `ptr` in the ASSIGNMENT RELATION): an array arg to an
    ITERATOR / element-reference param decays to `^arr[0]`, to a WHOLE-array-ref
    param passes `^arr` (autoRefPointee — the whole-ref via `^arr` is the convenience,
    EXACT rung 0); an array matching BOTH shapes picks the whole-ref over the element
    decay (cast rung 2) in argConvertCost — no longer ambiguous.
  * ARRAY from a tuple: `int a[3]=(1,2,3)`, `a=(4,5,6)`, multi-dim
    `int td[3][2]=((1,2),(3,4),(5,6))` (a NESTED tuple whose SHAPE — row × col —
    matches the standard-order dims). ELEMENT-AWARE: collectArrayElementNodes
    descends EXACTLY dims.size() levels and stops at the ELEMENT — so a scalar
    element flattens to leaves, but a tuple/array element stays an aggregate (this
    is how ARRAYS OF TUPLES `(int,int) a[3]` and TUPLE-OF-ARRAYS slots work; it
    replaced the old flatten-to-scalars + tupleMatchesArrayShape). A wrong nesting
    is "Array initializer shape does not match the dimensions of '<T>'"; each leaf
    widens into the element type. A SINGLE-ELEMENT array (flattened count 1 —
    `int[1]`, `int[1][1]`) takes a BARE SCALAR: the 1-tuple==scalar collapse leaves
    the lone element's initializer unwrapped, so checkValueAssign
    (isScalarIntoUnitArray) wraps it in dims-deep 1-tuples and routes it HERE. NOT a
    broadcast (`int[3]=5` stays rejected); a NON-scalar element (`(int,int) arr[1]`)
    is still un-spellable (todo). Also: array↔tuple VALUE copy both directions
    (`(int,int,int,int) t = a1` / `int a4[4] = t4`), incl. NESTED cross-form
    (`(int,int)[2] <-> (int[2],int[2])`) — lowered BY SLOT in desugar to per-leaf
    stores (see ASSIGNMENT RELATION); PARTIAL-index lvalues (sub-array assign
    `td[1]=(100,101)` + sub-array value read). LOWERING by position: a STATEMENT
    target (decl / assign / store) fills via emitArrayFromTuple (per-element store
    into the alloca); an RVALUE position (a `return (1,2,3)` for an int[3], or an
    operand like `fn() + (1,2,3)`) builds an `[N x T]` VALUE — emitExpr's kTupleExpr
    arm dispatches to emitArrayLiteralValue when the literal's type is an array (an
    array has no `.slots`, so the tuple-struct path would index empty). See
    [[project_v2_array_types]].
  * DESTRUCTURE — THE SOURCE MODEL (classify::desugarDestructure). Every form — COPY (`=`),
    MOVE (`<--`), SWAP (`<-->`) — lowers to PER-SLOT statements against the source, so each
    slot binds through the ordinary assignment path (dispatching a user op= / op<-- / op<-->).
    A destructure therefore reads its source ONCE PER SLOT, and the source's SHAPE decides how:
      - a tuple LITERAL is taken APART — element i binds to slot i directly, no temp. A
        DECLARING slot takes the element as its INIT (built in place); a LIVE slot is assigned
        from it. Elements keep their own nature: a LITERAL element flexes into the slot exactly
        as `int8 a = 1;` does (nothing is frozen at `int` first), and a CONSTRUCTION element
        bound to a live slot is materialized into a `_$delem` temp, since a live target cannot
        be assigned FROM a construction (no fresh slot to build into — see dispatchAssignInit).
      - an LVALUE source is INDEXED per slot — `src[i]`, cloned from the source EXPRESSION
        (cloneExpr), not re-minted from a name. So a NESTED slot recurses with `src[i]` as its
        sub-source and needs no machinery of its own: nesting works at any depth in ALL THREE
        forms, and mints no per-level temp. MOVE/SWAP require such a source (they null /
        exchange through the REAL storage); the predicate is isReReadableLvalue — isBareLvalue
        PLUS evaluation-count safety (an ident/field/deref/const-index chain), since `f()^` is
        an lvalue but re-runs f per slot.
      - anything else (a call, a chain) SPILLS to a `_$dsrc` temp: it must be evaluated exactly
        once. So must a LITERAL THAT READS A TARGET — per-slot stores run in order, so slot 0's
        store would be visible to slot 1's read, and `(sa, sb) = (sb, sa)` must SWAP. The alias
        guard (collectDestructureTargets / readsAny) is what keeps that true.
    Canon tuple/destructure.sl (semantics) + class/evaluate.sl blocks P/S4/V (object counts)
    and block Y (WHICH source got read, and HOW MANY TIMES: a nested rvalue source, a counted
    source evaluated exactly once, a spilling destructure per loop ITERATION, and a discard
    slot that drops its element's VALUE but not its EFFECTS).
  * THE SPILL FUNNEL (classify: `spillToTemp`). A SPILL materializes a source that must be
    evaluated exactly ONCE but is READ MORE THAN ONCE — indexed per slot, spread per field —
    into a temp local. Four sites hand-rolled the same three things (an Entry, a bare
    kVarDeclStmt, an ident reading it back); they now share one.
    THE DECISION to spill — is this source safe to re-read once per slot, or must it be
    evaluated once? — is likewise ONE helper: `spillIfNotReReadable` (the deep predicate
    `isReReadableOperand` -> `isReReadableLvalue`: a literal, or an access path with no side
    effect; anything else spills via spillToTemp and the caller places the decl). Every
    take-the-source-apart site funnels through it — `lowerAggregateConversion`, the class-field
    spread feeder (classifyStmt), and the slot-wise explode's `prepareOperand`. It replaced a
    SHALLOW per-site `isBareLvalue` gate that saw only the OUTERMOST node kind, so a side-
    effecting subscript or base (`arr[pick()]`, `get()^.f`) was cloned and re-run ONCE PER SLOT
    — a duplicated side effect, correct-valued but observable. The deep predicate recurses the
    access path (an index needs a const/ident subscript AND a re-readable base), so it spills
    exactly those. Canon tuple/combined.sl (the pick() block: subscript / base / move / explode /
    class-field-feeder positions, "pick" once per statement).
    THE TEMP'S LIFETIME IS THE CALLER'S ONE DECISION, and the funnel exists to make it one.
    A spill is a TEMPORARY: it dies at the SEMICOLON. Two placements do that:
      SEQ   — the temp is read by ONE expression (the statement's rhs). Park the decl ON THE
              NODE (`agg_conv_spill`, children = [decl, value]); desugar's liftSretCallExprs
              hoists it into the statement's kSeqExpr, whose teardown destroys it.
              (`_$cinit`: an aggregate conversion, and a class initialized from an rvalue
              aggregate — the source tuple spread across the fields.) The hoist must fire at
              EVERY consumer position, not just the decl / assign / return rhs: a STORE / MOVE
              source (`x <-- ((T) = arr[pick()])`) carries the same 2-child agg_conv_spill node,
              and codegen asserts on it (`kConvertExpr needs 1 operand`) if it is left inline.
              liftSretCallList's store/move arm scans for an agg_conv_spill child and hoists it
              (store/move sources are otherwise left in place); the move-into-live path was the
              last position to get this. Canon tuple/combined.sl (the `cmv` case).
      GROUP — the temp is read by SEVERAL SIBLING STATEMENTS. Put the decl and those
              statements in a BLOCK, and hoist any DECLARING slot OUT of it — a declared name
              has to outlive the block. (`_$dsrc`, `_$delem`: a destructure's per-slot stores.
              A nested destructure's declaring slots escape too, via the prelude.)
    Pushing the decl into the `prelude` is the third way and it is WRONG: a prelude statement
    is just another local of the ENCLOSING BLOCK, so the temp lives to the end of the SCOPE.
    Three sites did that — each having re-derived the lifetime by accident, which is what the
    duplication bought. Pinned by evaluate.sl block X.
  * A CLASS CAN ONLY BE COPIED INTO — it must EXIST first, so every binding is alloc, init,
    ctor, THEN the transfer. classify's `applyTransferSplit` peels a same-type class-bearing
    LVALUE out of a fresh decl's initializer and re-emits it as an assignment AFTER the decl,
    leaving the DEFAULT value in its place — the decl constructs a proper object, and the
    transfer copies into it. Otherwise the binding site FILLS the storage (which IS the copy)
    and only THEN runs the ctor hooks, so the constructor lands on top of the copied value.
    Applies PER SLOT of a tuple / array literal (a CONSTRUCTION slot still builds in place;
    only a copy is deferred), and to the whole value of any class-bearing target. It does NOT
    recurse into a class's FIELD tuple: a construction's args are field initializers and the
    ctor must SEE them, so that copy cannot be hoisted past it (readme-classes.txt / todo).
    A class CHAIN in a SLOT is peeled too (`(A,A) t = p + q`), and the reason is a limit of
    DESUGAR, not of the language: the tuple-literal distribution can hand a slot to a nested
    literal or a construction, but NOT to a chain — a chain's accumulator home is answered per
    STATEMENT, and a slot of a literal is not one. Unpeeled, the chain lifts to a temp, the
    tuple is formed from the temps, and the whole VALUE fills the storage, after which each
    slot's ctor runs ON TOP of the result (a writing ctor read back 99, not the sum). Peeled,
    the slot is constructed and the chain is assigned INTO it — an ordinary chain into a live
    target. At the ROOT a chain is NOT peeled: a decl whose whole rhs is a chain IS the
    accumulator (zero temps), which was always right.
    When the exploded rhs carries an agg_conv_spill SEQ (an operand that had to be evaluated
    once), the spill decls are LIFTED OUT and re-placed as a GROUP: the decl (now defaults)
    goes to the prelude, and the spill and the transfers share one BLOCK. Looking THROUGH the
    seq instead would leave the spill riding on the decl's rhs, so the temp would die at the
    DECL's semicolon — before the peeled transfers that read it.
    A chain slot always ASSIGNS into its constructed slot, even when the binding is a MOVE
    (a return's is): a move needs a source OBJECT to husk, and a chain has none — it IS the
    value. (Emitted as a kMoveStmt it reaches codegen unlowered and asserts.)
    THE RETURN SLOT IS STORAGE TOO, and it has the same rule — return_fn.sl's canon case 3
    spells the order out: `initialize ret^; ret^.ctor(); ret^ <-- a;`. Two halves, because an
    sret slot has no NAME for classify to address:
      - A tuple / array LITERAL with a transfer in a slot (`return (a, b);`) is BOUND TO A
        `_$ret` LOCAL (classify's return arm: `hasClassTransferSlot` -> `spillToTemp`), which
        gives the declarator funnel the target it needs — the peel above then orders each slot,
        and desugar's NRVO builds `_$ret` DIRECTLY in the caller's slot, so the name is free.
      - A whole-value LVALUE return (`return a;` from overlapping locals, `return p^;`) is
        ordered in CODEGEN: classify parks the class's DEFAULTS on the return node (children[1],
        via classZeroValue — codegen has no ClassInfo), and the sret arm FILLS them, runs the
        ctor, and only THEN emitInitFills the transfer. NRVO is the ELIDE of exactly that pair
        (canon case 2 — built in place, nothing to transfer), so an NRVO'd return emits neither.
        This is also the SAFETY NET for the first half: if NRVO declines `_$ret` (another
        returned-local is live there), the rewritten `return _$ret;` is an ordinary lvalue
        return and lands here — correct, one extra object.
    ONE SITE STILL HAS THE OLD WRONG ANSWER (todo.txt): that class FIELD. A GLOBAL also cannot
    be split (it has no statement list — `prelude` is null, hence the `is_global` guard — its
    initializer becomes a synth ctor instead), but it no longer needs to be: a global's
    initializer must be a CONSTANT (see the globals section), so a global class can only be
    built from a constant field list — BUILT IN PLACE, no copy to order wrongly.
  * A TUPLE OF CLASSES IS BUILT INTO THE STORAGE THAT OWNS IT — never materialized twice.
    Two halves. (1) DESUGAR: liftSretCallExprs, in an INTERCEPTED position (a decl / a return —
    storage the statement owns RAW), DISTRIBUTES a tuple literal instead of lifting its
    elements: a CONSTRUCTION element is unwrapped to its per-field tuple (the same unwrap
    liftSretCallList does for a root construction) and interception passes DOWN, so a nested
    literal of constructions never mints a temp. It does NOT pass to a live ASSIGN target — its
    slots are already objects, so they must be transferred INTO (op=), not built over. This
    generalizes past tuples: a construction as a class FIELD (`Holder h( B(3,4) )`) and inside
    a RETURNED tuple build in place for the same reason. (2) CODEGEN: emitInitFill distributes
    a class-bearing aggregate filled from a tuple literal SLOT BY SLOT (emitTupleFromTuple /
    emitArrayFromTuple's class arm) rather than building one whole value and storing it — which
    would BLIT past each element's op=. See readme-classes.txt, THE TRANSFER INVARIANT.
  * THE SLOT-WISE EXPLODE — A TUPLE DESUGARS TO THE OPERATION BY SLOT, ITERATIVELY AND
    RECURSIVELY (classify::explodeAggregateExpr / explodeAggregateAug). An ARRAY IS a
    homogeneous tuple, so this is every aggregate, and it is the ONLY aggregate machinery:
    `(a,b) + (c,d)` becomes the tuple literal `(a+c, b+d)`, and each element is then
    classified as an ORDINARY operation — class dispatch, literal flex, widen, the chain
    machinery — so NOTHING aggregate-specific survives past classify. A nested aggregate
    slot re-enters the rewrite when its own element is classified, so recursion is free.
    The result lands through the tuple-literal distribution the declarator funnel already
    has: element i is built into slot i of the storage that owns it.
    AN ARRAY STAYS AN ARRAY. The slots are carried in a kTupleExpr, which infers to a TUPLE
    type — so exploding an array RETYPED it, and every site downstream then saw a CROSS-FORM
    copy (array <- tuple) and lowered it by spilling the source to a temp and copying leaf by
    leaf (desugar's lowerAggCopyStmt). For a class-bearing array that temp cost a ctor and a
    dtor PER SLOT, copied in and immediately overwritten by the very operation that built it.
    The explode now re-forms its result as the ARRAY of the slot type (read off the SLOTS —
    widening may have changed the element type — with a nested array's dims folded back in).
    Covers `tuple op tuple`, `array op array`, mixed `array op tuple` (a mixed result is a
    TUPLE — the heterogeneous-capable shape), a scalar BROADCAST into any aggregate
    (`array + 1`, `100 - arr`, `tuple += 1`), SHIFT (an aggregate lhs shifts per slot; a
    scalar count broadcasts), the AUG-ASSIGN (which explodes into per-slot STATEMENTS, not
    one expression — the operator the author wrote is the COMPOUND one, so a class slot must
    reach its own `op+=`), and UNARY `+ - ~`. COMPARISON is still rejected on an aggregate
    (`(a,b) == (c,d)` is an open semantic question: a tuple of bools, or one all-slots-equal
    bool?), and so is `!`, for the same reason.
    THE ONE SHAPE RULE lives here: every aggregate operand must have the same slot count (a
    scalar broadcasts). Everything else — leaf types, narrowing, bitwise-on-a-float, a
    non-integer shift count — is asked of each SLOT by the ordinary arms, because a slot IS
    an ordinary operation. A nested shape mismatch needs no rule of its own: the nested pair
    explodes in its turn and asks the same question there.
    EVALUATED ONCE: an operand is read by every slot, so it goes through THE SPILL FUNNEL
    unless it is safe to re-read — the same trichotomy the destructure's source model uses (a
    tuple LITERAL is taken apart, a re-readable lvalue is indexed, anything else spills; a
    scalar broadcasts by cloning, or spills if it cannot).
    WHY IT IS HERE AND NOT IN CODEGEN — the reason aggregates kept breaking. A class
    operation needs an ADDRESS (a `^self` receiver, an sret destination). The old walkers
    (codegen::emitAggregateArith / emitAggregateShift, both DELETED) worked in the VALUE
    domain — extractvalue / insertvalue on SSA registers — where a slot has no address, so
    they could only ever emit a numeric instruction. And they did: a class-bearing aggregate
    emitted `add { i32 } %a, %b` — invalid IR, exit 0 (widen::commonType SUCCEEDS for two
    identical class types, so nothing caught it) — while the unary arm had no aggregate
    walker at all, so even `-(1, 2)` emitted `sub { i32, i32 } 0, %t`. Codegen now ASSERTS
    that an aggregate operand never reaches emitBinary. Canon tuple/anon.sl + array.sl +
    combined.sl (numeric), class/evaluate.sl block Z (classes).
    INC/DEC on an aggregate (`++array` / `--tuple`) also
    works — codegen's kBumpExpr walks the leaves and applies the SAME per-leaf bump
    (numeric ±1, iterator one element) it uses for a scalar, so it steps every leaf
    recursively in every PPID position (statement/expression, pre/post, complex
    lvalue, sub-phrase); classify accepts an aggregate whose leaves are each inc/dec-
    able (isIncDecable). Not the arith path — the bump IS the existing inc-dec, just
    leaf-walked.
  * ARRAY TYPES (`int[N]`): parseType parses sized dims, so array types compose —
    tuple slots `(int[3],int[4])`, alias RHS, params, returns `int[3] f()`. Every type
    site funnels through parseDeclarator(NamePolicy) — the UNIFIED declarator (see
    plan-declarator.txt): Required (var-decl / param / for-var, name-anchored
    `int x[3]`), Forbidden (return / alias-RHS / cast / conversion / sizeof / enum
    underlying), ListSlot (a tuple-TYPE slot — anonymous; a named slot is "too many
    names"), BindSlot (a destructure slot). reject_top_dim (set from the policy:
    Required / BindSlot, which bind a name) rejects a TOP-LEVEL sized dim — the size
    goes on the NAME; a dim nested in a `^` (`int[3]^`) or hidden behind an alias is
    always allowed. parseType is now internal (called only by parseDeclarator + its
    own const recursion); parseBracketGroup is the shared one-bracket dim primitive.
    A dim may be a const EXPRESSION in ANY type position: parseDeclarator always
    supplies a `dim_sink` into which a non-literal dim spells a provisional `[1]` and
    pushes its expr. constfold's
    bakeDimsWalk then folds + bakes them PRE-ORDER over the whole TypeRef tree
    (descends tuple slots + array elements, through ptr/iter/alias by handle-rebuild)
    — name dims outer, type-position dims inner. ALIAS targets refresh: resolve
    expands an alias use eagerly with the provisional `[1]`, so bakeNodeDims records
    an AliasRefresh when an alias target bakes and re-points every use (node types at
    walk-entry, entry types on bake) within the fixpoint, so a sizeof of an alias use
    folds against the real size (resolve keeps the kAlias wrapper on sizeof(Alias) so
    the refresh can reach it). INDEXING is a per-segment walk in emitElementAddr:
    dispatch on the CURRENT type each step (array dim -> GEP; tuple slot -> struct
    GEP), composing alias-element nested arrays + array->tuple->array chains; the old
    codegen rank check (a buggy duplicate of classify's per-level over-index) was
    deleted.
  * FOR-TUPLE `for (v : tuple)` over a HOMOGENEOUS tuple: resolve understands it
    (understandForTuple, retagging the kForEnumStmt carrier to kForTupleStmt) and
    desugar lowers it (lowerForTuple) to a kForLongStmt walking an iterator
    `_$iter = <T[]><void^>base` (the void^ bridge — so a `ref^` deref iterable
    dodges addr-of-through-deref); a VARIABLE iterates in place (no copy, mutable
    by-ref writes back), a LITERAL or rvalue call spills to a `_$ftmp`. A
    non-primitive element forces by-ref; a by-ref var's pointee must match the
    element type. The loop var may be EXPLICITLY tuple-typed (`for ((int,int)^ e :
    pairs)`) — parseForVarHead accepts a `(`-led tuple type via looksLikeTupleTypeDecl
    (same as a param), which is how a const-dim rides a for-var type (a tuple slot).
  * #x desugars (grammar parseUnary) to the 5-tuple `(##file, ##line, ##type(x),
    ##name(x), ^x)`; x must be an lvalue. Passing an rvalue tuple to a reference
    param (`dump(#x)`) materializes it in a temp — emitCall brackets such a call
    in @llvm.stacksave/stackrestore so a materializing call in a loop doesn't leak.


CLASSES — moved to the companion file readme-classes.txt

  The class cluster (CLASSES + CTOR/DTOR; CLASSES: NEW / DELETE / SIZEOF + .~();
  CLASSES: AS A NAMESPACE + LOCAL; NAMESPACE ↔ CLASS; SINGLE INHERITANCE; RE-OPENING
  CLASSES + THE EXTERNAL FORM) now lives in readme-classes.txt. See it for the METHODS,
  CONSTRUCTION, hoisted/local-class, inheritance, and re-open notes.


GLOBALS (single-TU; the guiding principle: globals FALL OUT of the scope machinery)

  A global variable is a `kGlobalVar` entry registered in WHATEVER FRAME it is declared
  in — file, namespace, class, or function body. Bare access, `::` (the unnamed global
  root), and `Enclosing:member` all fall out of ordinary frame-chain name resolution;
  no per-scope code. Storage is an `internal @`-symbol (a `ptr`), so a global slots into
  the SymTab uniformly beside a `%`-alloca local — after emitFunction seeds the globals,
  every access site (read / lvalue / index-base / assign / address-of `^g` /
  for-iteration `for (v : g)`) treats them identically. Because a global IS a storage-backed variable, resolve's `^` addressability
  gate accepts kGlobalVar alongside kLocalVar (the only two storage kinds). All three
  address helpers must fire the lazy-init touch gate — emitLvalueAddr (bare var / deref),
  emitElementAddr (index / field), AND kAddrOfExpr; the last needed emitTouch ADDED to its
  bare-ident branch (2026-07-08), else `^` of a lazy global — INCLUDING a method receiver
  `^Global:g` (a `c.m()` call passes `^c`) — skipped construction, so the method's
  mutations were wiped when the first read later fired the ctor. So `^g` / `^garr[i]` /
  passing `^g` to a `T^` param / a method call on a global object all construct the global
  first, then hand out `@g`.

  SPELLINGS (all desugar to the same two shapes — a plain global or a named group):
    * SHORT / BARE — `global [Type] name = init;`, or at namespace/file scope the
      keyword is OPTIONAL (`int x = 0;` IS a global). Typed or inferred.
    * NAMED GROUP — `global name(decls){body}` is a NAMESPACE (`kNamespaceDecl`,
      is_global); members are `name:member`. Because namespaces nest in every scope
      (incl. functions), a named group works everywhere with no special casing.
    * ANONYMOUS GROUP — `global (decls){body}`: members promote into the ENCLOSING
      scope (bare / `::` / `Enclosing:member`), not under a group name. This is the ONE
      thing the resolver can't express natively, so resolve's `explodeAnonGlobalGroups`
      (the only globals-specific transform) dissolves it BEFORE registration: members
      splice in as bare siblings; a LAZY anon group's ctor/dtor move into a generated
      plain namespace `$glazy<id>` whose bodies resolve the members bare up the frame
      chain. After it, everything downstream sees only plain globals / named groups.

  A GLOBAL'S INITIALIZER IS DATA, AND DATA IS CONSTANT (classify's `isConstantInit`, asked
  at the TOP of the kVarDeclStmt arm, of EVERY global, whatever its type and whatever scope
  it is declared in — file / namespace / class / function-internal / group member). A
  literal, `nullptr`, or a tuple literal of those. constfold has already run, so folded
  arithmetic, a substituted const and an enum member ARE literals by now.
    * NOT a CONSTRUCTION EXPRESSION: `global Widget w = Widget(5);` is an error, though the
      identical LOCAL is fine. Its arguments are constant, but a construction is exactly the
      thing that RUNS CODE. A class global takes its fields as DATA — `global Widget w(5);`
      (the declarator form: `(5)` is the FIELD LIST), or the same fill spelled `= 5` /
      `= (7, 9)`. That holds at any depth: a construction as a field's value or as an
      aggregate element is rejected too; the field-list spelling (`= ((5,7), 2)`,
      `= ((1,2),(3,4))`) is the one that builds them.
    * NOT a read of another variable (`= other_global`, `= ^g`) and NOT a call (`= f()`,
      `Widget w(f())`).
    * The CLASS is never policed beyond the initializer: its field DEFAULTS and its CTOR
      BODY are code and may do whatever they like (allocate, call, touch other globals —
      canon does all three). That is what the lazy gate is for, and it is where a global
      built FROM another global belongs: `global (Widget c) { _() { c = w_; } ~() {} }`.
    * This is POLICY, not soundness — the first-touch gate orders cross-global reads
      correctly (a global's ctor touching another fires that one's gate first). It is
      rejected because an initializer that quietly reads another global looks like data and
      behaves like code. Say it in a ctor instead.
    * It also makes the class-copy-into-a-global ordering bug UNREACHABLE: a global class
      can only come from a constant field list, which BUILDS IN PLACE, so no copy exists to
      order wrongly (see "A CLASS CAN ONLY BE COPIED INTO" — the LOCAL's fix does not reach
      a global, whose initializer is lowered into a synth ctor rather than statements).
    Canon test/assign/global.sl (its positives AND the eleven `is not a constant expression`
    negatives).

  STATIC vs LAZY — the routing keys on the DECLARED TYPE, never on the shape of the
  initializer. (constfold's `tryCaptureConst` used to ask the latter: it is the CONST
  capture, reused for a global's static init, so ANY global whose rhs folded to a literal
  was dragged onto the static path and range-checked against its declared type — and
  `global Widget w = 5;` reported "Constant 'w' value '5' does not fit declared type
  'Widget'". 5 was never meant to fit a Widget; it is the Widget's first FIELD. It now bails
  for an array / tuple / class global, matching what `needsSynth` says below.)
    * A scalar with a foldable constant init is STATICALLY allocated (`@sym = internal
      global <ty> <const>`); constfold captures the literal. A global is static iff it
      carries NO touch gate (`touch_symbol` empty — the sole codegen discriminator for
      emitting a folded constant init vs zero-init storage).
    * A COMPOUND global (array / tuple / class, or any aggregate containing a class) is
      LAZY (the all-compound-lazy policy): zero-init storage + codegen SYNTHESIZES a
      ctor (fills the array/tuple init or the class construction-args + field-defaults,
      then fires the ctors on first touch) and, for a class-containing type, a dtor.
      classify runs a scope-level global through the SAME construction funnel as a local
      (classifyClassInit / classZeroValue), which types the aggregate init and builds
      the class field-default tuple.
    * A GROUP is the lazy unit — its compound MEMBERS are constructed as one on first
      touch of ANY member. desugar's `finalizeGroup` builds a GlobalGroup carrying the
      members in declaration order (`member_ids`) plus the user `_()`/`~()` symbols
      (`user_ctor_symbol` / `user_dtor_symbol`); codegen's synthesized group ctor thunk
      constructs each member in order THEN calls the user ctor, and the dtor thunk calls
      the user dtor THEN destructs members in reverse. A lone compound global is the
      degenerate one-member group (`synth_global_id`), built through the SAME per-global
      helper (`emitGlobalConstruct` / `emitGlobalDestruct`). `needsSynth` decides which
      members are gated: a foldable scalar stays static, a class/aggregate rides the gate
      — but a group WITH a user hook forces EVERY member onto the gate (so any access
      fires the ctor). A hook-less group still forms one shared gate around its compound
      members; a hook-less ANON group gets a shared group id in `explodeAnonGlobalGroups`
      so its members group too. A hook-bearing group MUST declare a member (grammar
      rejects a memberless one — nothing would trigger its gate).
    * FIRST-TOUCH gate + LIFO teardown: each lazy group has a sentinel bool + a touch
      thunk (checks/sets the sentinel, REGISTERS the group dtor thunk with a runtime LIFO
      list BEFORE running the ctor, so registration order == ctor-invocation order — each
      group registers exactly ONE dtor thunk). Access sites call the touch thunk; a
      never-touched group is neither built nor destroyed.

  THE `global;` SCOPE STATEMENT — a real scope-registered lifetime (a `DtorScope`
  entry): at its scope's exit the `__$global_dtor_all` registry walker runs. Auto-
  inserted at the top of `main` when omitted; may be placed explicitly (incl. a nested
  block to scope teardown earlier); `main`-only. resolve's region check flags a global
  accessed OUTSIDE the open scope and a SECOND `global;`.

  FUNCTION / METHOD-INTERNAL — a block-scope global is a SCOPED STATIC: `kGlobalVar` in
  the body frame, so it persists across calls, is reached bare inside, and is invisible
  outside (a function is not a namespace). Two bodies' same-named internals are distinct.

  DEFERRED (Phase 8): a global declared in a `.slh` header is visible to all linked
  files; a `.sl` global stays file-local. Rides the cross-TU `.slh` propagation work.


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
            `arr[i] = v`, name-led statements): it scans the (qualified) name, then
            a type-suffix run, then requires the var name. The suffix run + trailing
            name is the shared typeSuffixesThenName helper (used by BOTH this gate
            and the `(`-led looksLikeTupleTypeDecl): it skips a MAXIMAL, interleaved
            run of `^`/`^^` (reference levels; `^^` is one logical-xor token read as
            TWO) and bracket groups — empty `[]` (iterator) OR sized `[N]`/`[a,b]`
            (scanned by bracket depth) — mirroring parseType's real suffix chain, so
            `Space:Dir x`, `Integer^ ref`, `Integer[] iter`, `Vec[5]^ v`,
            `(const int)[5]^ p` all parse. The TRAILING IDENTIFIER is the
            decl-vs-statement discriminator: a store/call puts an OPERATOR after the
            suffix run (never a bare name), so `arr[i] = v` stays a store and
            `p^ = v` a deref-store — bracket EMPTINESS no longer matters (a bare
            `a ^ b` still reads as a reference decl `a^ b`, a bare XOR being no
            statement form); a top-level sized dim (`(const int)[5] x`) is now
            RECOGNIZED as a decl and reaches parseType's "size belongs on the name"
            diagnostic, not a statement misparse. An
            array DIM (`Int nums[4]`) goes after the name -> the plain `Ident
            Ident` path; a bracket may hold a COMMA dim-list (`int g[3,5]`, the
            natural-order form) — each bracket's dims are appended REVERSED
            (`[a,..,z]` -> `[z]...[a]`), the same transpose parseSubscript applies
            to a `g[x,y]` read/store. A literal dim is validated for positivity
            here (a zero/negative literal -> "Array size must be a positive integer
            constant"); a const-EXPRESSION dim is validated in constfold;
            `alias Name = type;` + bare `alias Ns;` decls; namespace decls
            (`Name { members }`) and qualified member decls
            (`const int Space:kSix = 6;`, `alias`/`enum` likewise); enum decls
            (`enum [type] [Name] ( m1 [= v], ... )`, name may be qualified
            `Class:E`); function defs/decls with typed
            param lists; var-decls with optional leading `const` (file
            scope or function scope) — incl. a TYPELESS const (`const name =
            expr`, detected by `=` immediately after the name, parseType skipped
            so constfold infers the type); statements (var-decl incl. the
            `<ident> <ident>` typed-decl shape and a `<--` default-move-init form
            (`T x <-- y`, the default_move_init flag — `<-->` swap is not a decl), assign,
            aug-assign, move (`a <-- b`) / swap (`a <--> b`) — the name-led lvalue
            chain (array element / tuple slot / class field / deref / composed) takes
            `=`, the aug-assign family, `++`/`--`, and move/swap UNIFORMLY (lhs as an
            expr child); finishMoveSwap on both the bare-name and chain paths,
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
            `switch (value) { clause... }` into a kSwitchStmt
            (children[0]=scrutinee, [1..]=kCaseClause); the value is required.
            There is NO `case` keyword: a clause is a LABEL-LIST + a mandatory
            `{ }` body block + an optional trailing `continue;` —
            `label (: label)* : { body } [continue;]`. parseCaseClause stores the
            labels as children[0..n-2] (each a const-expr; null = a `default`
            label, mixable into the list) and the body block as children.back();
            a trailing `continue;` is recorded as clause.text == "continue" (a
            fall-through into the next clause). Each label is parsed under the
            `case_label_` flag so a qualified enum-member label (`Dir:N:`)
            resolves its trailing `:` as the terminator, not a qualifier
            (parseQualifiedNameCaseLabel scans the maximal `:`-chain; the body is
            always `{`-led so a single qualified label needs no rewind). A token
            after a label's `:` that cannot begin another label and is not `{`
            reports "Expected '{' to open the clause body." A loop carries an
            optional `:label` (parseOptionalLabel)
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
            (kConvertExpr — a type-keyword right after `(` opens it; ONE
            lookahead [looksLikeConvTarget, factored from the former
            looksLikeTupleConvTarget + looksLikeIdentConvTarget] opens a
            tuple-led target `((...)...)` OR an identifier-led target
            [a `::`(global)/`:`(member)-qualified class/alias name — user-named /
            namespaced targets], by a lead that is either a balanced `(...)` or
            a qualified name, then any type-suffix chain `^`/`[]`/`[N]`, matched
            if the next token is `=`;
            parseConvertChain parses the target onto return_type and recurses
            for chain links `(A = B = expr)`, right-to-left, no inner parens),
            prefix/postfix ++/--, full binary set
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
            placement prefix [newParenStartsPlacement disambiguates a `(`-led ELEMENT
            type (tuple `(int,int)`, grouped `(const int)^`) from a placement address
            by the token AFTER the balanced `(...)`: a type-start => placement, else
            the `(...)` is the element], the element type [the ONE parseType with
            TopDim::StopBeforeSized — ANY variable type, stopping before the trailing
            sized `[n]` which is the runtime alloc count], an optional `[n]` count
            [plus further `[d]` dims that are the element's trailing dims — multi-dim
            `new int[n][2][2]`], an optional `(args)` ctor tuple; children[0]=count-or-
            null, [1]=addr-or-null, [2]=ctor-args-or-null)). `delete p;` is
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
            kAlias entries; resolveTypeRef substitutes an alias chain to
            its underlying (cycle-detected, structurally over the interned type
            handle), and resolveDeclType rewrites every
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
            through resolveDeclType -> resolveTypeRef -> resolveQualifiedType
            and wired at the var-decl declared-type + cast-target sites (a
            flat-tok fallback covers the sites that don't pass it yet).
            Owns the `##type` operand dispatch: the kStringifyType arm looks up
            the operand (resolveName for a bare name / resolveQualifiedRef for a
            qualified one — both return the entry for ANY kind, erroring only on
            a missing name) and branches on entry kind. A TYPE-NAME operand (a
            kAlias, or an enum's kNamespace type facet) resolves its entry's type
            through resolveTypeRef and deep-strips to the underlying, stamped on
            return_type (so `##type(Integer)` / `##type(Space:Dir)` -> the
            underlying); a VALUE operand (kConst /
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
            uninitialized pointer); the operand is ANY pointer expression (classify
            checks the pointer type). An UNRESOLVED ident / wrong-kind name is
            reported by resolveExpr / classify.
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
            namespace-member enum registers in registerScopeNames (which
            registers type-introducing members — enums, nested namespaces, class
            names — before consts/functions, so a member's type may name a sibling enum
            regardless of order) and resolves inits in resolveScopeBodies.
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
            pre-pass and the main kVarDeclStmt arm (constfold infers a foldable
            SCALAR's type; a non-scalar rhs is deferred to classify, which routes it
            to a kLocalVar + deepConst const variable).
            An end-of-body sweep
            then reports any body-declared local never read: "Unused local
            variable 'x'." if never written, else "Local variable 'x' set but
            never used."; gated on hasErrors so a use-before-init or dup
            diagnostic isn't trailed by a spurious unused report. ARRAYS and TUPLES
            (in-place aggregates — isInPlaceAggregate) use a separate MAY-set
            (assigned_arrays): such an aggregate can't be fully initialized in one
            statement and a fill loop's element writes wouldn't survive the must-
            set's loop join, so a SLOT/element write assigns the WHOLE aggregate
            (monotonic, never rolled back) and a read requires only SOME earlier
            write — in assigned_arrays (a slot write) OR initialized_locals (a
            whole-value `=` init; arrays route their init to the former, tuples to
            the latter). Reading before ANY write still errors; a `^arr[i]`
            address-of marks it assigned, and an iterator-base store (`it[i]=v`)
            READS the iterator (the pointer is dereferenced), not writes it. An
            augmented assign is a read-modify-write, so resolve read-marks the
            lvalue chain's base local (markLvalueBaseRead) — a bare `x += v`, a
            deref `p^ += v`, and an index `a[i] += v` all count the target as used
            for the sweep (resolveStoreTarget alone marks only the write side).
            A SUBSTITUTED const (foldable scalar) and params are exempt (substituted
            away / not in body_locals); a const VARIABLE (non-scalar) is a real local
            and IS swept. Control-flow joins are modeled by a Completion
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

            NESTED FUNCTIONS ARE SCOPED, NOT TOP-LEVEL-ONLY. resolveStmtList runs
            THREE pre-passes per SCOPE, in this order: relocateOutOfLineMembers,
            registerLocalClasses, registerNestedFunctions — then the statements —
            then resolveNestedFunctionBodies. So a nested function may be declared in
            ANY scope of its host (the body's top level, a bare block, an if-arm, a
            loop body, a switch case). It lives in the frame it is WRITTEN in:
            visible there INCLUDING before its own definition (the signature
            pre-pass), gone when the scope closes, and it captures that scope's
            locals. Two same-named nested functions in disjoint if/else arms are NOT
            a duplicate — different frames. The BODY pass runs after the scope's
            statements with the frame still OPEN, so a nested function may reference
            any local of its scope declared anywhere in it (FORWARD capture); it also
            runs on the ABRUPT path (a definition after a `return` is unreachable
            CODE, but its body must still resolve or classify meets an unstamped
            ident). Classes register BEFORE functions so a signature may name a
            scope-local class. Deep nesting is rejected via capture_floor >= 0 ("am I
            inside a nested function's body"), which catches an inner function hidden
            in a block too. Captures are SOUND at any depth: a capture is the host
            alloca's ADDRESS, every alloca lives in the ONE function frame, and the
            function can only be CALLED from inside the scope where its captures are
            live. A METHOD / OPERATOR / ctor / dtor IS a kFunctionDef and takes this
            same path, so a function nests in any of them; a nested function inside a
            method INHERITS `self` if it needs it (a bare field name rewrites to
            `self.field`, and `self` is an ordinary local of the method's frame, so
            both capture like any other host local). Canon test/function/nested_fn.sl.
            [Both pre-passes used to scan a function body's DIRECT children only — a
            block-nested function was then never registered: calling it said "Unknown
            function", and NOT calling it left its body entirely unresolved and
            CRASHED classify on an unstamped identifier. Fixed 2026-07-12.]
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
            the Nth enclosing LOOP outward; N>=1), or naked (nearest enclosing
            loop) — switch is fully TRANSPARENT to both break and continue (it
            pushes no loop_stack frame and is not a break target), so a naked break
            and `break 1;` are equivalent inside a switch — and folds the current
            init-set into THAT frame's
            break/continue accumulator (∩, top-seeded via a `seen` flag; a do-while
            / for consume them, a pre-condition while ignores them). It stamps
            Node.loop_levels = hops outward to the target for codegen. NO flavor of
            break/continue is allowed directly in a for-update clause (the
            in_for_update + for_update_floor guard fires first). Errors: count <1 /
            exceeds nesting (caret on the count literal), no enclosing loop labeled
            <name>, "A 'break'/'continue' statement must be inside a loop."
            A kForLongStmt
            (long-form `for (varlist) (cond) {update} {body}`; the canonical for
            node — other for shapes desugar to it) opens ONE for-scope holding the
            varlist, with the update and body as sibling nested blocks (3 frames;
            the body may shadow a for-var — normal lexical scoping, one rule for
            all four for shapes). A TYPELESS varlist decl (empty return_type) needs
            an initializer: WITH one it becomes a kAssignStmt routed through the ONE
            declarator funnel (registerDeclarator DeclareOrReuse — reuse an in-scope
            assignable variable, else a fresh inferred-init local; a non-assignable
            target rejects "Cannot assign to <noun> '<x>'." exactly as an assignment
            would); with NO init it is a structural error "A variable declaration
            needs an explicit type or an initializer." — independent of what the name
            resolves to (`int x; for (x)` still errors; a varlist slot may not be a
            bare touch). Resolved body-then-update
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
            kForTupleStmt for desugar to lower; a CLASS iterable (a var, `ptr^`, a
            construction, a call — any type that is a kSlid) is LOWERED HERE by
            understandForClass into a kForLongStmt over the class's protocol methods
            (size/op[] or begin/end/next) and re-resolved, so classify infers the
            synthesized method calls [a call minted in desugar is never classified —
            hence lower-at-resolve, not retag-for-desugar]; a real enum (a kNamespace with an
            underlying type) is rebuilt as a kForRangedStmt over the enum's
            first..last DEFINED members — find the first/last kConst members by id
            (definition) order in its ns_frame, build `for (var : Enum:first .. <=
            Enum:last) {body}` (members referenced by qualified name → normal
            resolve/constfold fills their values), tagged range-derived so a
            descending/empty enum (first > last) trips the empty-range "Invalid
            range." check on the enum name; then resolved through the RANGED path
            (lowered in desugar, so its `_$end` is minted fresh — no clobber across
            nested typeless enum loops). A kSwitchStmt resolves
            the scrutinee, then each clause's labels and its body from the entry
            set S (any clause is a direct dispatch target, so direct entry is the
            weakest join input). NO switch frame is pushed (switch is transparent
            to break/continue). There is NO implicit fall-through: a clause that
            completes Normally is an EXIT path unless it carries a trailing
            continue (clause.text == "continue") into a non-last clause — a
            continue on the LAST clause falls off the bottom and exits. The
            after-set is the ∩ over those exit paths plus — default-less / empty —
            the no-match path = S; a switch with a default and no exit path is
            Abrupt. An empty
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
            tryCaptureConst infers a FOLDABLE-SCALAR typeless const's type (a strong
            rhs takes its type, a bare-literal rhs is WEAK -> weakDefaultType preferred
            spelling with the narrowest nominal kept under the hood; a non-scalar /
            unfoldable rhs is left for classify) and marks explicit-typed
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
            the source: non-coercible operands for ! && || ^^ (and the &&= ||= ^^=
            aug-assigns), an if / while / for condition not coercible to bool,
            non-numeric shift sides, bitwise on
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
            scrutinee is integer-class (float rejected); each label in every
            clause's label-list must be an integer constant (constfold folded it)
            that FITS the scrutinee type (literalFitsContext — an out-of-range /
            sign-mismatched label is rejected, never emitted as a truncated `iN`)
            and is unique by value (full 64-bit dedup, so 'a'==97 and 1+2==3
            collide, with a "first case here" note); default is singular. A switch
            is a return-terminator (endsInReturnNode) iff it has a default, no
            clause has an escaping break (containsBreak — the same test codegen
            uses), and EVERY clause's exit reaches a return: a clause whose body
            returns is fine, and a non-returning clause is fine ONLY if it carries
            a trailing continue into a LATER clause (the fall-through chain must
            end in a return; a non-returning last clause, or one with no continue,
            escapes without returning).
            Per-arg type inference at call sites uses the resolved
            callee's param_types (cached on the kCallStmt/kCallExpr by
            resolve) as context. A kCallExpr's inferred_type is the
            callee's return type; a void return used as a value is rejected
            here. Pointer-cast rules (Phase 4) live here as two predicates:
            ptrImplicitOk (a bare assignment may only STRIP info — nullptr->any,
            any->`void^`/`intptr`, iterator->reference of the same pointee; an ARRAY
            source decays to its ELEMENT pointer `^arr[0]`, rewritten in
            checkValueAssign) and
            ptrExplicitOk (a `<Type^>` cast additionally bridges through a
            buffer-class pointer [`void^`/`int8^`/`uint8^`] or `intptr`, and
            reinterprets iterator<->reference of the same pointee; two unrelated
            non-buffer pointers must chain through `void^`; only `intptr` bridges
            pointer and integer). checkPtrAssign enforces the implicit rule at
            EVERY pointer-assignment site — kVarDeclStmt init, kAssignStmt, and
            kStoreStmt (a references-array element / deref slot) — gated on a
            pointer being involved (pure-numeric assignments keep the width path).
            The kCastExpr arm validates the explicit rule and stamps the target;
            a `<const>` / `<mutable>` qualifier cast (grammar marks it via the
            node's text — the angle brackets hold only the keyword) instead DERIVES
            the result from the operand (internConst / removeConst), requiring a
            pointer operand. Value-preserving: const is erased in IR, so codegen's
            kCastExpr is unchanged (widen::convert strips const -> a no-op).
            The kConvertExpr arm (`(Type = expr)`, Phase 4) infers the operand
            with NO context (it retypes, never flexes) then dispatches to
            checkConvertCompat — a lockstep recursive walk of target vs source:
            pointer-like target -> "may not be a pointer type"; void -> "must be
            a value type"; a TOP-LEVEL class target branches off BEFORE this walk to
            lowerClassConversion (default-construct a `_$cret`, FILL it from the
            source, lift like a construction — the literal "assignment to a temp";
            the fill is the user op= if one matches, else a SAME-CLASS default
            whole-value copy; the 12th declarator site, see plan-declarator.txt); an
            AGGREGATE target (tuple or array) branches off to lowerAggregateConversion,
            which desugars it BY SLOT — the kConvertExpr BECOMES a tuple of per-slot
            sub-conversions `((T0=src[0]), ...)`, each re-inferred (a class slot reuses
            the class path, a primitive slot the leaf grid, a nested aggregate
            recurses) — so a class leaf at ANY depth converts, cross-form works (an
            array IS a homogeneous tuple: source may be a tuple OR array, matched by
            slot/element COUNT via aggregateSlotCount) with any nesting, and a
            side-effecting source is SPILLED once (agg_conv_spill); aggregate-vs-scalar
            and a slot-count mismatch reject ("Cannot convert ..."); a leaf
            (primitive) target -> a value source converts to any value target,
            a pointer/iterator source converts ONLY to `bool` or `intptr` (else
            "a pointer converts only to 'bool' or 'intptr'"), a non-value source
            is rejected outright. It stamps the target as inferred_type (even on
            the error paths). Because the result is a strong typed value, an
            over-narrow assignment of it is caught by the same
            checkStrongConstAssign as any typed value.
            Move / swap (Phase 4): kMoveStmt classifies like a store — infer the
            lhs lvalue, the rhs in that context, then checkPtrAssign +
            checkStrongConstAssign (so a move COPIES under assignment rules; a
            narrowing move rejects). kSwapStmt requires the two operands' deepStrip
            types be IDENTICAL (no widening — a symmetric exchange can't convert
            both ways), else "Swap operands must be the same type". classify also
            rejects a SELF-swap / SELF-move with "Cannot swap a value with itself" /
            "Cannot move a value onto itself". isSameLvalue is STRUCTURAL: a bare
            variable (same entry), a deref (operand same), a class field (same name +
            base same), and an index (base same + a PROVABLY-same index — a literal or
            the same bare var; isSameIndex peels a PPID bump to its operand, so
            `a[i++] <--> a[i++]` matches as a self-op too — it lowers to
            `a[i] <--> a[i]; i++; i++`). A non-provable index (`a[f()]`) is a genuinely
            different element (left). resolve owns
            the lvalue rule: resolveMoveSwapLvalue rejects a non-lvalue (a swap rhs
            is a general expression, so `x <--> 7` would otherwise crash codegen)
            — BOTH swap operands and a move's LHS must be lvalues ("A swap operand"
            / "A move target" must be an lvalue); the accepted forms are a bare
            ident, an array element, a tuple slot, a class field (kFieldExpr), or a
            deref. a move's RHS is a plain read, so an rvalue source is allowed. DA: a move lhs is a pure write (need not
            be pre-init); a swap reads+writes both (both must be init).
            A SUB-AGGREGATE operand works too: a PARTIAL array index (a sub-array
            row `g[i]`) or a tuple SLOT (whether the slot is a tuple or an array) is
            a valid swap/move operand — emitLvalueAddr threads allow_partial=true for
            the swap/move sites so emitElementAddr returns the slice/slot ADDRESS,
            and the value at the operand's type is exchanged/moved — a hook-bearing
            class (or a tuple/array with a class leaf) dispatches PER LEAF to the
            class's `@<Class>__$swap` / `__$move` op (`emitAggregateSwap`/`Transfer`),
            a POD operand keeps the inline whole-value load/store (subject to swap's
            exact-type rule, so two slots of differing type reject). codegen-only —
            classify already types the partial index.
            kNewExpr: a heap element must be sized — a primitive / pointer / array /
            TUPLE (compile-time typeByteSize, the tuple laid out with LLVM-default
            struct alignment) OR a CLASS (runtime __$sizeof, so it IS allocatable);
            void / a tuple with a class slot / unsized -> "Cannot allocate" (carets
            the element type, name_tok). An array size must be integer-class; a
            placement address must be a buffer-class pointer (isBufferClassPtr, the
            cast set void^/int8^/uint8^);
            `new T(args)` ctor args (children[2]) belong to a single class; an ARRAY
            new takes a size-matched initializer tuple for a LITERAL element count
            (typed as the `T[k]` array via classifyArrayFromTuple — same shape check
            as the stack `T arr[k](...)` form; a runtime count -> "non-literal size
            cannot take an initializer", a non-class scalar -> "Only a class takes
            constructor arguments"). The result type is element + (array ? "[]" :
            "^"). kDeleteStmt's operand must be a pointer type. See
            readme-classes.txt "CLASSES: NEW / DELETE / SIZEOF" for the construct/
            destruct lowering. Overload resolution across same-name Function entries has
            LANDED — one shared rankOverload core (pickOverload for fn/method calls,
            findClassOperator for op=/op<--/op<-->) ranks each arg on a convert-cost RUNG
            ladder (exact 0 / alias 1 / cast 2 / smallest same-sign 3-5 / cross-sign 6-8
            widen) and scores a candidate by the MAX rung over its args, reporting a tie
            via reportAmbiguity, citing each conflicting declaration.
            THE OVERLOAD SET ITSELF IS CHECKED AT ITS DECLARATION —
            checkOverloadDefaultCollisions, run right after the signature pre-pass (the
            point where every param type, incl. one INFERRED from its default, and every
            num_required are final, so it is order-independent). A default parameter makes
            a candidate's arity a RANGE, so two overloads can admit the same arg count;
            when they ALSO agree on the parameter types up to that count, no call could
            ever tell them apart, and the PAIR is the error ("Ambiguous overloads of 'fn':
            a call with N arguments matches both."). `fn(int)` + `fn(int, int = 0)` is the
            canon case — either fn(i) is ambiguous (fn(int) uncallable) or it picks
            fn(int) (b's default unreachable), so neither reading is taken. It compares
            ENTRIES, so methods get the rule by construction (a `_$recv` sits in both
            prefixes and cancels); an identical-param_types pair is skipped (a forward decl
            + its definition, owned by the duplicate-definition check). Canon
            test/function/overload_fn.sl + test/class/overload_cls.sl.
  desugar   parse tree -> ast (separate node-type set). Today: identity
            copy that propagates every annotation classify and constfold
            stamped (nominal_type, inferred_type, op_type, resolved_entry_id,
            params, param_types, file_id, tok).

            THE LOWERING PASSES RUN PER FUNCTION — AND "EVERY FUNCTION" INCLUDES
            NESTED ONES. run() collects every kFunctionDef in a program subtree
            (collectFunctionDefs: top-level AND nested, recursively) and runs the four
            passes over that list — liftSretCallList (the sret/construction lift + the
            class-operator chain expansion), lowerAggregateList, lowerStatementList
            (PPID), analyzeNrvo — each with the function's OWN return type. A nested
            function is a STATEMENT inside its host's body, so the old
            `for (fn : program->children)` loops never gave it a turn and its
            statements reached codegen UNLOWERED: `++`/`--` asserted ("inc/dec survived
            desugar's PPID pass" — no classes needed), as did class temps, aggregate
            copies and operator chains. The pass BODIES needed no change and still
            treat a nested kFunctionDef as an OPAQUE statement (lowerStatementPPID has
            an explicit `case kFunctionDef: return;`; collectReturns stops at one) —
            that "don't descend" discipline is CORRECT, since a nested function owns
            its own return type and sret slot. What was missing was only the outer loop
            handing each body its turn. Fixed 2026-07-12; see [[project_nested_fn_desugar]].

            Three rewrites are live.
            (1) aug-assign (`lhs op= rhs`): a BARE-name lhs -> `lhs = lhs op rhs`
            (synthesized IdentExpr + BinaryExpr inheriting the aug-assign's
            classify-stamped types). A COMPLEX lvalue (array element / tuple slot /
            class field / deref / composed; carried as children[0]) binds the leaf's
            ADDRESS ONCE into a hidden `_$lv` reference — `^lvalue` for an index /
            field target, the pointer itself for a deref target — then a kBlockStmt
            does `_$lv^ = _$lv^ op rhs`, so a side-effecting index evaluates exactly
            once (address-once). Both the bare-name and complex aug-assign forms
            inherit the classify-stamped types so codegen sees the rewrite as if
            classified directly. (`++`/`--` on a complex lvalue is NOT routed here --
            it flows through PPID below, like every other inc/dec.) (2) PPID: a post-copy pass extracts
            ++/-- per phrase, replacing each with a read; also drops parse-
            only nodes (alias, namespace) and hoists namespace member
            functions to program scope with entry-id-derived symbols (no
            cached canonical-name strings). lowerStatementPPID is an EXHAUSTIVE
            switch over the statement kinds (var-decl / assign / store / move / swap
            / destructure / return / call / expr lower their operands; break /
            continue / delete / dtor / fn-def are explicit no-ops; an assert
            backstops a future kind -- no silent default). Statement-level bumps
            splice as sibling kExprStmt bump-statements around the statement (post
            AFTER the store -- the statement is the phrase: `arr[k++] = v` ->
            `arr[k]=v; k++`, and a swap `x++ <--> y++` -> `x <--> y; x++; y++`); a
            bump inside a sub-phrase (call args, && / || rhs, each tuple-literal /
            destructure slot) stays in a synthesized kSeqExpr {pre... value post...}
            so a short-circuited or per-slot bump never fires. A COMPLEX-LVALUE
            operand (`arr[i]++`, `++p^`, a class field) flows through THIS pass too:
            it binds the leaf address once into a hidden `_$lv` reference (the aug-
            assign pattern), the read becomes `_$lv^`, and the bump is an ADDRESS-
            based kBumpExpr (children[0] = `_$lv`, codegen loads through it, reusing
            the scalar int/float/iterator step). A post-inc captures at the read
            point (a nested kSeqExpr {decl, _$lv^}); a pre-inc captures with the bump
            in the pre-list. An AGGREGATE operand (`++tuple`, `--arr`, an array-of-
            tuples element, even an array-of-iterators) needs NO new lowering — it
            rides the same read + kBumpExpr; codegen's kBumpExpr (emitLeafBump factored
            out) GEP-walks the tuple/array leaves (emitAggBump, the emitNullLeaves
            shape) and applies the per-leaf scalar/iterator step to each, so it works
            in every position above. classify gates it with isIncDecable (every leaf
            inc/dec-able). collectVarDecls (codegen) walks expression subtrees so a
            seq-buried `_$lv` decl is still hoisted to the entry block, and the
            kSeqExpr arm emits its address store. So bare/complex x pre/post x
            statement/expression all flow through this ONE PPID path -- both grammar
            shortcuts (the postfix `arr[i]++;` -> aug-assign rewrite, the bare-ident-
            only prefix) are gone. The statement-bump
            splice (lowerStatementList) recurses into a kBlockStmt, a kIfStmt
            (lowerIfStmt: the condition is a self-contained phrase whose bumps fire
            before the branch, and the arms recurse), a kWhileStmt / kDoWhileStmt
            (lowerWhileStmt: the condition is a phrase re-tested each iteration, the
            body recurses), a kForLongStmt (lowerForLong: varlist initializers +
            condition are phrases, the update + body are statement lists), and a
            kSwitchStmt (lowerSwitchStmt: the scrutinee is a phrase) so a bump
            inside them splices within that scope, not at function scope. The OTHER for-shapes
            now lower HERE too (no longer in grammar/resolve): lowerForRanged /
            lowerForArray / lowerForTuple build a kForLongStmt subtree, minting
            helper locals (`_$idx`/`_$end`/`_$step`/`_$iter`/`_$ftmp`) with fresh
            resolved_entry_ids from one program-wide counter seeded at entries.size()
            — resolve/classify UNDERSTAND the short form, desugar lowers it (no
            parse-tree mutation; codegen is node-driven). The for-tuple iterator is
            `<T[]><void^>base` (the void^ bridge), so a `ref^` deref dodges
            addr-of-through-deref.
            (3) aggregate copy by slot (lowerAggregateList, runs after the aug-assign
            / class-lift phases, before PPID): a CROSS-FORM or leaf-WIDEN aggregate
            copy (an array IS a homogeneous tuple) at a kVarDeclStmt / kAssignStmt /
            kStoreStmt is rewritten into per-leaf kStoreStmts over a form-agnostic
            kIndexExpr chain — `dst[i] = src[i]`, recursing while the dst slot stays
            an aggregate that differs from the src slot, bottoming out at scalar (or
            same-type sub-aggregate) leaves. Indexing is form-agnostic (codegen's
            emitElementAddr dispatches array-dim vs tuple-slot), so `dst[i]`/`src[i]`
            walk arrays and tuples alike; a non-lvalue source spills once to `_$agg`.
            A same-type whole copy stays a single store (the trivial base case).
            kMoveStmt and kReturnStmt lower here too: a cross-form / leaf-widen MOVE
            adds a per-leaf source null (emitAggNullLeaves), a RETURN materializes a
            `_$ret` temp of the return type and copies into it by slot — so codegen's
            kMoveStmt / kReturnStmt only ever see a same-type whole-value op (a whole
            CLASS one — or a tuple/array WITH a class leaf — codegen then dispatches PER
            LEAF to `@<Class>__$copy` / `__$move` / `__$swap`, the memberwise transfer op).
            Future rewrites (receiver shapes, more operator dispatch) slot in as their
            phases land.
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
            kIfStmt emits emitToBool on the condition (a numeric is `!= 0`, a
            pointer-like — `^`/`[]`/anyptr — is `!= null`, a float is `une 0.0`; the
            unary `!` MIRRORS it, comparing a pointer-like to `null` not `0`) + a
            conditional br to then/else/merge labels (no phi — definite-assignment rides the hoisted
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
            to one block per clause (source order; one dispatch entry per label so
            a label-list maps several values to the one block; default's block =
            the switch instr default, or exit when there is no default). There is
            NO implicit fall-through: a non-terminated clause body brs to the EXIT
            unless it carries a trailing continue (clause.text == "continue"),
            which brs to the NEXT clause's block (a continue on the last clause brs
            to exit). break/continue inside a body bind to the ENCLOSING loop — the
            switch passes the enclosing LoopCtx straight through (no switch ctx,
            consistent with resolve pushing no switch frame); a trailing continue
            after a returning body is naturally inert (endsTerminated suppresses
            its br). The exit block gets an `unreachable` terminator only when
            nothing reaches it (every clause returns, has a default, no exit br).
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
            for the SAME-type case (swap passes through desugar untouched; a cross-
            form / leaf-widen move is desugared by slot first, so codegen sees only
            same-type moves). A SAME-type MOVE from an lvalue computes the source
            ADDRESS ONCE, LOADs the value (a widen / implicit-pointer-cast applied if a
            scalar move's source and dest types differ), stores it into the lhs, then
            emitNullLeaves walks the rhs's structured type and `store ptr null`s every
            pointer / iterator leaf through that SAME address — recursing by GEP into
            nested tuple AND CLASS slots and array elements (a class is a named tuple;
            typeHasPointer prunes pointer-free subtrees) — so a side-effecting source index
            (`a <-- g[bump()]`) runs ONCE and the source is left valid; an rvalue source
            (isAstLvalue false) has no address and is a pure copy (emitExpr+store). The
            move-INIT form (`T x <-- y`, kVarDeclStmt) is the same — desugar skips
            default_move_init, so a cross-form / leaf-widen default-move-init reaches codegen and the
            single load is followed by the form-agnostic aggregate convert. A SWAP
            loads both lvalues into SSA temporaries and stores them crossed (no
            stack temp; a whole-value load/store handles tuples; both loads precede
            either store, so an aliased swap is safe). emitLvalueAddr gives an
            operand's address (a bare var's alloca / emitElementAddr / a deref's
            pointer); a default-move-init decl (`T x <-- y`) nulls after the var-decl store.
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
            A kConvertExpr (`(Type = expr)`) reaching codegen is now only a SCALAR
            (primitive leaf) target — neither a CLASS nor an AGGREGATE target arrives
            here: classify+desugar lowered a class target to a lifted `_$cret` temp
            (construct + fill), and an AGGREGATE target to a tuple of per-slot sub-
            conversions (classify::lowerAggregateConversion), so the node was already
            replaced. So this arm emits the operand at its own type and calls
            emitConvertWalk, which for a scalar leaf bottoms out at widen::convert-
            Explicit. (emitConvertWalk is still FORM-AGNOSTIC — it recurses on the DST's
            slots (cgAggSlotCount/cgAggSlotType) and extractvalues the source slot by
            index, identical for an LLVM array `[N x T]` and a struct — but its
            AGGREGATE arm is now BYPASSED for conversions, since aggregate targets
            desugar per-slot in classify.)
            convertExplicit is the FULL value grid (sibling to convert, which
            only widens): trunc / sext / zext / fptrunc / fpext / fptosi /
            fptoui / sitofp / uitofp, a same-width int change is a no-op (sign
            reinterpret is free), `->bool` is a nonzero (`icmp`/`fcmp une`) /
            non-null test, a pointer source is `ptrtoint` (->intptr) or the
            non-null test (->bool). It never reports — classify pre-validated
            — and asserts on any state classify should have caught. The result
            is then flexed into dest via widen::convert.
            kNewExpr: `new T` -> `call ptr @malloc(i64 sizeof(T))`; `new T[n]` ->
            `mul i64 n, sizeof(T)` then malloc (sizeof(T) is typeByteSize — a
            primitive / pointer / array / tuple; a CLASS uses its runtime __$sizeof);
            placement (children[1]) -> the address itself, no allocation. An assert
            guards an unsized element (classify gated it). A class object is
            constructed THROUGH THE FUNNEL (emitConstructAt, register_dtor=false since
            delete owns the dtor): a single object from children[2] (an absent init
            default-constructs); an array WITH a size-matched initializer builds the
            whole `T[k]` in one emitConstructAt (the array<->tuple bridge distributes
            it + runs each element's ctor); an array with NO initializer broadcasts the
            default value per slot, each finalized via emitConstructed. delete of a
            class runs its complete dtor — see readme-classes.txt. kDeleteStmt: load the pointer, `call void
            @free(ptr)`, store null back to its alloca. malloc / free are declared in
            the module preamble next to printf.

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
            findInFrame / entryType APIs that resolve
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
                  Builds to ../bin/slidsc. Objects in ../build/compiler/.
