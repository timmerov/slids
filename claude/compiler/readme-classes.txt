compiler — CLASSES (companion to readme.txt)

Per-stage current-state notes for the CLASS cluster, spun out of readme.txt to keep
the map file navigable. Covers: class definition + ctor/dtor; new / delete / sizeof
+ explicit `.~()`; class-as-namespace + local / hoisted classes; the namespace<->
class scope abstraction; single inheritance; and re-opening classes + the external
form. Other clusters stay in readme.txt (pipeline, design principles, type
representation, assignment relation, tuples/arrays, stage/product files); cross-refs
to those name "readme.txt <SECTION>".


CLASSES + CTOR/DTOR (landed this phase; spans every stage)

  * A class IS a named tuple. `Name(field-list){body}` (grammar parseClassDef: an
    identifier directly followed by `(` at file scope — a function is `Type name(`).
    The field list reuses the param-list parser (kParam nodes, defaults in
    children[0]). resolve registerClass builds a ClassInfo (field names / types /
    the stable kParam nodes / def location) and interns the kSlid type CARRYING its
    field slot types (widen::internSlid) — so the whole tuple aggregate path
    (construct, store, slot access, llvmForRef -> literal struct) is reused and
    codegen needs NO symbol table; the layout rides on the type. requireKnownType
    accepts a registered-class leaf, INCLUDING inside a tuple (leafIsKnownClass has
    a kTuple arm — a tuple type is known iff every slot is a built-in OR a known
    class). A class as a TUPLE SLOT or ARRAY ELEMENT initializes BY SLOT through the
    SAME constructClass path as a class FIELD and a single `Point pt = 0`: classify's
    kTupleExpr arm constructs each class-typed slot from its init value (a scalar /
    tuple is the slot-class's ctor input, field-arity validated per slot), so
    `(Point,Point) t = (0, (3,4))` builds each Point in place. Aggregate INIT is by
    slot, iteratively and recursively, the leaf reusing the single-element class
    init — no separate aggregate leaf path (the rewritten slots are layout-matching
    construction values, so desugar/codegen are unchanged).
  * INFERRED FIELD TYPES — a typeless field with a DEFAULT (`Class(x = 1, y = 3.14)`)
    infers its type from the default's preferred type (x -> int, y -> float, width
    follows the value), like an inferred local / param / const. STAGING: a field type
    is a kSlid LAYOUT slot (attached by internSlid at resolve registerClassBody) and a
    default isn't folded until constfold, so resolve DEFERS — it registers a kNoType
    slot (a typeless field with NO default still errors "needs an explicit type"); then
    a classify pre-pass (classifyClassSignature, in classifyScopeSignatures, BEFORE any
    method body / construction reads the field type) folds the default, infers, and
    RE-INTERNS the handle (internSlid by name+def_id, slots excluded from structKey, so
    every reference updates). An inferred field is always PRIMITIVE (a const-expr default
    can't be a class), so the resolve needs-ctor/dtor fixpoint that ran on the kNoType
    slot stays correct — no re-run. Canon test/class/field.sl; [[project_inferred_field_types]].
  * `.field` is a kFieldExpr (grammar postfix `.name`); classify types it via the
    ClassInfo; desugar lowers it to a kIndexExpr over the field's slot index, so it
    never reaches codegen (slot access by name). `^field` address-of walks
    kFieldExpr chains (resolve) and emitElementAddr GEPs a kSlid slot.
  * CONSTRUCTION (classify classifyClassInit) normalizes every init form to a
    per-field tuple: each field = init slot, else the author default (read LIVE off
    the kParam node — constfold may have replaced it), else default-constructed
    (classZeroValue: 0 / 0.0 / false / nullptr for a scalar/pointer; RECURSIVELY a
    TYPED array / tuple value whose leaves are themselves default-constructed — a
    class leaf via constructClass with ITS field defaults; only void / an
    unregistered class errors. The synthesized aggregate nodes carry inferred_type,
    else codegen's array field-init asserts on an untyped element). A class-typed
    field constructs RECURSIVELY (constructClass): a scalar/tuple is the sub-class's
    ctor input filled with ITS defaults; a same-class value copies. The `=` form
    SPREADS its tuple slot-to-field; the call form keeps each arg whole. An EMPTY
    slot in the CALL form (`Class c(,2,3)` / `(1,,3)`) is a nullptr in the provided
    list: the field loop early-branches on it, consumes its position, and fills it
    from the field default (fdefault / recursive sub-class / classZeroValue) exactly
    as a tail under-fill would — so a leading/interior omission means "default this
    one" (grammar parseCallArgs allow_empty; the two construction-declarator sites
    only, a TRAILING comma rejected). The `= (tuple)` value-init form has no empty
    slots — it goes through buildClassFromValue's op= path, not this loop (todo).
    A class — OR an array/tuple whose leaves are classes (widen::hasInPlaceClass, recursing array
    elem + tuple slots, stopping at a pointer/iterator) — is definitely-initialized
    (DA) and default-constructed IN PLACE even with no initializer: resolve marks it
    initialized, and classifyStmt (and the per-field loop) synthesizes the
    construction via classZeroValue, recursing through arbitrarily MIXED arrays/
    tuples to every buried class leaf, so a class is never uninitialized. Covers a
    LOCAL, a class FIELD of array/tuple-of-class, and deep mixed nesting; canon
    tuple/combined.sl (default + literal + variable forms).
  * NAMELESS CONSTRUCTION: a `Class(args)` call whose callee resolves to a kClass
    (not a function) is a CONSTRUCTION, not a call — resolve flags `is_construction`
    (resolveCallTarget accepts a kClass target instead of "is a variable, not a
    function"); classify (classifyConstruction) wraps the args as the `Type name(args)`
    init tuple and runs classifyClassInit. desugar then lowers it onto the EXISTING
    construction machinery (no new codegen): FORM 1 (a bare `Class(args);` statement)
    becomes a synthetic `_$nameless` kVarDeclStmt in the current scope — ctor + the
    scope's reverse-order dtor; a hookless trivial class used this way is a no-op and
    errors ("A nameless class statement has no effect"). FORM 2 (a construction used
    inline — a method receiver, a field read, a call arg) is lifted to a `_$cret`
    temp (liftSretCallExprs); for a kCallStmt/kExprStmt the temp decl is block-wrapped
    WITH the statement so its dtor runs at the SEMICOLON. A construction as a
    DECL/RETURN rhs (incl. the `<--` move-init) builds in place (RVO — one ctor/dtor,
    the kAssignStmt unwrap is excluded). Any OTHER position — an if/while condition, a
    store/move/swap operand, a re-assignment `w = Class(...)`, a method-call VALUE
    `Class(...).method()` — is rejected cleanly (codegen's is_construction guard at
    emitExpr/emitCall, and the parser's "method call in an expression" error), never
    miscompiled. Detail: test/class/nameless.sl.
  * BARE CLASS NAME = DEFAULT CONSTRUCTION: a class name with NO parens is `Class()`
    (a zero-arg default construction). In a VALUE position resolve rewrites a bare
    kIdentExpr that resolves to a kClass into a zero-arg construction kCallExpr
    (is_construction) — but ONLY when EVALUATED; an UNEVALUATED operand (sizeof /
    ##type, the resolveExpr `unevaluated` flag) keeps the name as the TYPE. In a
    STATEMENT position grammar accepts a bare `Name;` as a zero-arg kCallStmt flagged
    `parenless`. Model: `Name;` = "evaluate Name and DISCARD". resolveCallTarget turns a
    parenless kClass into a construction (form 1); resolveUserCall turns a parenless
    VALUE name (local / param / const) into a discarded READ — a kExprStmt holding the
    kIdentExpr — which "uses" the name (marks read_locals, so the unused-local sweep stays
    quiet) and evaluates to nothing, exactly like a postfix `arr[0];`. A parenless FUNCTION
    name is a call missing its parens ("Function call is missing parameter list '()'." — a
    function is never a value); a namespace / type is "'X' is not a statement." So a stray
    name is never silently CALLED, but a value name is a cheap "use it" no-op.
    No new codegen — construction reuses the zero-arg machinery, the read the expr-stmt path.
    Canon: `NoInitClass;` / `(NoInitClass, 7)` / `NoInitClass a[2] = (NoInitClass,
    NoInitClass)` in test/class/nameless.sl.
  * INIT FROM A TUPLE-LIKE VALUE: a class also initializes from any aggregate VALUE
    source — an array / tuple variable or constant, a sub-array row, a function
    return, an op result — spread across the fields BY SLOT (a class IS a named
    tuple): a partial source fills the lead and defaults/zeros the rest, and a slot
    feeding a class-typed field recurses. classifyClassInit spreads a side-effect-free
    lvalue in place (`src[i]` element reads); classifyStmt first spills any source
    that ISN'T a bare identifier (a call / op / `g[bump()]`) into a `_$cinit` temp so
    it is evaluated ONCE — spliced via classifyStmtList's prelude, classify's only
    statement-minting hook. A SAME-TYPE source is NOT spread — it is a whole-object
    COPY, or a MOVE for `<--`: classifyClassInit short-circuits it (deepStrip equality
    with the class type), codegen dispatches the transfer to the class's COPY / MOVE
    FUNCTION and STILL runs the ctor, so a copied/moved object is constructed exactly
    once and balances its dtor. COPY / MOVE / SWAP AS FIRST-CLASS OPERATOR METHODS: the
    default op=(Self^) / op<--(Self^) / op<-->(Self^) are REAL memberwise methods — resolve's
    `synthesizeClassTransferOps` mints them (unless the user declared one, which shadows) as
    kFunctionDef + kFunction entries (marked Entry.synthesized) in the class's op tree, body
    one statement per field `_$recv^.fi OP _$src^.fi` (copy / move / swap). It runs at the
    resolveScopeBodies choke point — the SINGLE per-class body pass every driver funnels
    through (file-scope, hoisted, reopened, derived, virtual, class-nested, AND function/
    block-LOCAL classes), so EVERY class is covered by one mechanism. Each per-field stmt
    dispatches THAT field's op, so a class field recurses into its own transfer (memberwise:
    `@Outer__$copy` CALLS `@Inner__$copy`) and a primitive/pointer leaf bottoms out at a
    plain store/move/swap. desugar's methodSymbol renames op= / op<-- / op<--> (user OR
    synthesized) to `@<Class>__$copy` / `__$move` / `__$swap`, so definition and every call
    agree; codegen just CALLS that symbol (`emitCopy`/`emitMove`; a tuple/array walks PER LEAF
    via `emitAggregateTransfer`/`emitAggregateSwap`, so a class element runs its op).
    THE TRANSFER INVARIANT: a whole-class transfer ALWAYS runs the class's transfer function.
    NEVER a blit. The gate is STRUCTURAL — `widen::hasInPlaceClass` ("is a class physically in
    this storage?") — at all THREE transfer sites (emitInitFill's move arm + its copy arm +
    kSwapStmt). It used to be BEHAVIORAL (`typeNeedsHook`, "does a ctor body exist?"), and a
    class can answer NO to that: a class with no ctor/dtor fell past the gate into an inline
    load/store TWIN that re-implemented the transfer and silently skipped the class's operator
    — so a hook-less class's user `op<--` never ran. Fixed 2026-07-12; the twins are
    unreachable for a class-bearing type and only pure POD (no kSlid anywhere in the type)
    still load/stores. A class transfer now has exactly ONE implementation — the operator body
    — so there is no twin left to drift out of sync with it. Pinned by evaluate.sl's Triv (a
    hook-less class whose op= / op<-- PRINT) and Sw (its op<--> too, plus a tuple of them:
    hooks are irrelevant, aggregates walk per leaf).
    A FOURTH site had the same hole, by OMISSION rather than by a wrong gate: a class-bearing
    aggregate filled from a tuple LITERAL (`(Trk, Trk) t = (a, b);`). Neither transfer arm
    fires — a literal is not an lvalue — so it fell through to the WHOLE-VALUE store at the
    bottom of emitInitFill, which builds one struct value (loading each element) and stores
    it: a blit, right past every element's op=. The whole-tuple form (`t = u`) dispatched
    correctly, so the two spellings disagreed. Fixed by making the literal a DISTRIBUTED fill:
    emitTupleFromTuple (and emitArrayFromTuple's class-element arm) GEPs each slot and
    recurses into emitInitFill, putting every element back on the funnel — an lvalue element
    runs its op= / op<--, a CONSTRUCTION element builds directly in the slot. Gated on
    hasInPlaceClass, so a POD aggregate keeps the cheap whole-value path. Pinned by
    evaluate.sl block V.
    A FIFTH site asked the same wrong question, in the SEQ temp path: emitSeqEffects
    constructed a phrase-scoped temp in place only when it WAS a class (`form == kSlid`), so a
    spilled TUPLE OF CLASSES fell into the scalar store path — where a class-bearing sret call
    has no destination to build into ("Returning a class by value in an expression position").
    Same fix, same predicate: hasInPlaceClass. "Is a class physically in this storage?" is the
    question at EVERY site that constructs, transfers, or destroys; "is this a class?" is not.
    NOTE the contrast with CONSTRUCTION,
    which stays gated on typeNeedsHook — that one really is asking "is there a ctor body to
    call?", and a hook-less class truthfully has none. The two predicates are NOT
    interchangeable.
  * A CLASS CAN ONLY BE COPIED INTO — so it has to EXIST first. Every binding is
    alloc, init, ctor, THEN the transfer. This is NOT the order the funnel produces on its
    own: a binding site FILLS storage and then FINALIZES it (emitInitFill, then
    emitConstructed's hooks), so a fresh decl whose source is an lvalue had its CONSTRUCTOR
    RUN ON TOP OF THE COPY and silently threw it away — `D dx = dy;` on a class whose ctor
    writes its own field yielded the ctor's value, not dy's. classify PEELS the transfer off
    the declaration (`applyTransferSplit` -> `splitTransferInit`) and re-emits it as an
    assignment AFTER it: the decl initializes to DEFAULTS wherever a copy used to sit (so the
    ctor sees a properly initialized object), and the transfer copies into the constructed
    object. Same idiom swap-init has always used (default-construct, then re-dispatch as an
    ordinary swap), now generalized to `=` and `<--`. It applies PER SLOT of a tuple / array
    literal, so a CONSTRUCTION slot still BUILDS IN PLACE and only a copy is deferred —
    `(Ord(1), wa)` builds slot 0 and copies into slot 1. What decides whether a copy happens
    AT ALL is the SOURCE, not the operator: an RVALUE (a call / construction / chain result)
    has no object to copy FROM, so it elides and builds in place. Pinned by evaluate.sl
    block W (class Ord — its ctor WRITES its field, which is the only way the order is
    OBSERVABLE; every other counting class merely prints, which shows a wrong ORDER but not
    a wrong ANSWER, and is why this lived so long).
    THE PEEL ALSO BUILDS AN op= CONVERSION IN PLACE (2026-07-15). A `(Class = value)` conversion
    slot (from buildClassFromValue — the class-from-value funnel below) is peeled like any
    transfer: splitTransferInit retargets the conversion's op= FILL onto the slot lvalue and
    drops the `_$cret` construct, so a tuple / scalar value op='s directly into the default-
    constructed slot — no temp, no copy — at the root decl, a tuple slot, and an array element
    alike. Outside a decl / return (no peel) the conversion falls back to its `_$cret` temp.
    THE SAME RULE AT THE RETURN SLOT (2026-07-13). An sret slot is storage like any other, and
    return_fn.sl's canon case 3 already dictated the order — `initialize ret^; ret^.ctor();
    ret^ <-- a;` — so a non-NRVO return that MOVED into the slot and then ran the ctor on top
    was a plain spec violation. It reached the slot by a spelling classify could not address (an
    sret slot has no NAME), so the fix is in two halves: a returned LITERAL with a transfer in a
    slot is bound to a `_$ret` local and goes through the peel above (NRVO then builds `_$ret`
    in the caller's slot — the name is free); a whole-value LVALUE return is ordered in codegen
    from the class DEFAULTS classify parks on the return node. NRVO is the ELIDE of that pair
    (canon case 2: the local IS the slot, so there is no transfer at all), and it is untouched.
    Pinned by return_fn.sl class Ret (again a WRITING ctor — every other class in that file
    only prints, which is why a live miscompile sat under a green golden).
    A SINGLE class returned from a VALUE joins the same binding (2026-07-15): `return (7,8)` /
    `return 5` where the return type is a class the value op='s / field-lists is bound to `_$ret`
    too (the gate widened past hasClassTransferSlot, which sees only aggregate return types), so
    the value->class assignment the generic return path would REJECT instead runs the declarator
    funnel (buildClassFromValue) and NRVO's into the caller's slot. A CONSTRUCTION return
    (`return P(1,2)`) and a whole-value lvalue return are excluded — the arm below already builds
    / transfers them. canon construct.sl (retTuple / retScalar).
    NOT DONE, and NOT the same problem: a class FIELD initialized from a class LVALUE
    (`Holder h( c )`) is still filled and then constructed over. A construction's arguments
    are FIELD INITIALIZERS, and a ctor must see its fields ALREADY INITIALIZED (evaluate.sl
    Q4's Hook computes `a_ + b_` in its ctor body), so a field's copy cannot be hoisted past
    the enclosing ctor the way a tuple slot's can — it has to land BETWEEN the field's own
    ctor and the enclosing ctor body, inside emitConstructed's hook recursion, where the
    initializer expressions no longer exist. todo.txt.
    A GLOBAL had the same wrong answer (`global Add gb = ga;` read back the ctor's value, not
    ga's) and CANNOT be fixed the same way — a global declaration is not lowered here at all;
    its initializer becomes the body of a synthesized LAZY CTOR (desugar's collectGlobals), so
    there is no statement list to split the transfer into. That is what classify's `is_global`
    guard says. It no longer needs fixing: a global's initializer must be a CONSTANT (readme.txt,
    the globals section), so `= ga` is now rejected outright and a global class can only be
    built from a constant field list — BUILT IN PLACE. The bug is unreachable, not repaired.
  * WHAT DECIDES WHETHER A COPY HAPPENS AT ALL IS THE SOURCE — never who wrote the operator,
    and never the shape of the target. dispatchAssignInit dispatches EVERY matching op, user
    or synthesized (findClassOperator finds a same-type op= for every class). The one thing
    that skips the operator is the ELIDE, and its rule is: a FRESH decl target initialized
    from a same-type class RVALUE (`isBareLvalue==false && exact`) BUILDS IN PLACE — the
    call / construction constructs straight into the slot, so there is no object to copy FROM
    and no copy to make. Everything else transfers: an LVALUE source is a genuine copy, a
    NON-EXACT source is a convert, and LIVE storage is copied into (it already exists — that
    is the whole of "a class can only be copied into", above).
    A class RVALUE INTO LIVE STORAGE therefore works in every position — `x = Op(11)`,
    `x <-- mkOp()`, `arr[0] = mkClass()`, `r^ = Class(2)`. The source is materialized as a
    TEMPORARY and transferred in through the target's operator; the temp dies at the
    SEMICOLON. It is not spilled by classify (that gave the temp enclosing-BLOCK lifetime):
    it is left as the operator call's ARGUMENT, and desugar's ordinary argument lift
    (liftSretCallExprs, block-wrapped by liftSretCallList) makes the temp — the same path
    `fn(Class(1))` takes. These four positions used to be REJECTED ("Constructing a class in
    this position is not yet supported" / "Returning a class by value in an expression
    position"), on the grounds that a live target has no fresh slot to build into. True, and
    that is the reason for the ELIDE — not a reason to refuse the TRANSFER. Canon
    nameless.sl block 34 + return_fn.sl.
    (This supersedes the earlier synthesized-op bail, which elided a DEFAULT copy but not a
    user one — so `arr[0] = mkClass()` compiled for a class with a user op= and was rejected
    for a class without one, which is not a distinction the language makes.)
    There is NO codegen whole-value blit fallback (that
    loop + its g_defined_*_syms gate were deleted; nothing is left to fall back to). A
    CROSS-TYPE operator (`op=(Other^)` / `op<--(Other^)`, case 4) is a distinct method
    dispatched by classify, NOT renamed. canon test/class/operator.sl + return_fn.sl.
  * A `(Class = src)` VALUE CONVERSION binds a FRESH temp from src — the "assignment to a
    temporary variable" model. classify::lowerClassConversion default-constructs a `_$cret`
    and FILLS it from the source by dispatching `_$cret.op=(src)` (findClassOperator +
    makeOpCallStmt, exactly the op= a decl-init `Class x = src` would run). findClassOperator
    surfaces every op= — a user op=(T)/op=(T^) AND the synthesized default op=(Self^) — so a
    same-class source resolves to the memberwise default copy with no bespoke fallback here.
    desugar lifts the [construct, fill] pair like a construction, so the temp destroys
    through the ordinary class-temp lifetime. A conversion is thus as permissive as the
    class's op= overloads (value via op=(T), pointer via op=(T^)) PLUS the same-class default
    copy; a source no op= accepts is a clean reject. It is the 12th declarator binding site
    (plan-declarator.txt "THE CONVERSION-TARGET TEMP"); an identifier-led grammar trigger
    (looksLikeConvTarget) routes user-named / namespaced / alias / virtual targets. An
    AGGREGATE target with a class leaf converts PER SLOT (classify::lowerAggregateConversion
    desugars to a tuple of per-slot sub-conversions), so a class leaf at any depth / form /
    cross-form / same-class reshape / spilled source reuses this path. test/assign/typeconv.sl.
  * CLASS-FROM-A-VALUE — op= VS FIELD-LIST, ONE FUNNEL (2026-07-15). A class slot built from a
    VALUE (a tuple / scalar, not a same-class copy) chooses between an op= ASSIGNMENT and a
    field-list CONSTRUCTION, and the choice lives in ONE place — classify::buildClassFromValue:
    when a USER op= accepts the value's type it mints a `(Class = value)` conversion (op=,
    above); otherwise it field-list-constructs (constructClass). This is the value-position twin
    of dispatchAssignInit, so `Class c = (1,2,3)` (root decl), a tuple SLOT `(C,C) t = ((1,2),
    (3,4))`, and an array ELEMENT `C a[2] = (...)` all reach a user op= identically — iteratively
    and recursively (a nested aggregate op='s every leaf). The conversion the funnel yields is
    peeled IN PLACE by splitTransferInit (the peel bullet above): it retargets the conversion's
    op= fill onto the slot and drops the `_$cret` temp, so the slot is default-constructed then
    op='d with NO temp and NO copy. EXCLUDED from op= (they stay their old paths): a same-class
    VALUE (a copy — the whole-value store), and a class FIELD (constructClass field-list — a
    field's transfer cannot hoist past the enclosing ctor; todo.txt). THE TWO SPELLINGS ARE TOLD
    APART IN THE PARSER: `Class c(args)` and `Class c = (tuple)` both lower to one tuple child, so
    grammar sets `construction_init` on the `(args)` form — ALWAYS field-list, never a value op=
    (the `= Class(args)` construction->tuple rewrite sets it too, so a `(5)` collapsing to a
    scalar cannot match op=(int)). Supersedes the old "a declaration from a tuple ignores the
    class's op=" gap. canon construct.sl (TupleInit / ScalarOp / Boxed; pair / row / mix / nest).
  * A CLASS FIELD THAT WOULD NEED A TRANSFER IT CANNOT HAVE IS REJECTED (2026-07-15), not silently
    field-listed past the operator. A field's transfer cannot hoist past the enclosing ctor (the
    class-FIELD exclusion above), so rather than blit and skip a user operator, classifyClassInit's
    field loop diagnoses the three shapes that would otherwise do so: (a) a VALUE-INIT field
    (`Super s = ((1,2,3),4)`) whose field class has a matching user op= — "cannot dispatch
    'Class.op='"; (b) a COPY of a same-class LVALUE into a field (`Holder h(c)`) — "cannot be
    initialized by copying"; (c) a MOVE of a same-class RVALUE into a field (`Holder h(mk())`) —
    "cannot be initialized by moving". The copy/move gate is `sub.needs_ctor || sub.needs_dtor ||
    userSelfTransferOpId(field, op) >= 0`: the last term (a DIRECT scan of the class + base frames
    for a NON-synthesized self-transfer op `op=(Self^)` / `op<--(Self^)`) catches a TRIVIAL-BUCKET
    field class — a user op= / op<-- but NO ctor/dtor — that needs_ctor/needs_dtor and
    findClassOperator both MISS (findClassOperator does not surface a trivial-bucket class's
    self-op), and whose blit would silently skip the operator. EXEMPT (legal): an in-place
    CONSTRUCTION slot (`Holder h(C(1))` — field-lists, one ctor, no transfer), a truly trivial POD
    field (a memberwise blit is byte-identical — nothing to skip), and the whole-aggregate SPREAD
    idiom (`Pair p = mkPair()` — a whole-object transfer, a different operation). This is the
    MITIGATION until the construction interleave lands (field hooks -> field transfers -> own ctor;
    todo.txt): a clean compile error instead of the old silent wrong-order blit. Canon construct.sl
    neg_field_copy / neg_field_move / neg_field_value_init_op / neg_field_*_oponly / neg_base_copy.
  * CTOR/DTOR are scope HOOKS, not the constructor — fields are initialized first,
    the ctor runs after, the dtor at scope exit. `_(){}` / `~(){}` parse as
    kFunctionDef with an implicit receiver param `_$recv` (`Name^`); a bare field
    name in the body rewrites to the spec `self.field` = `_$recv^.field` (resolve
    method_fields fallback — locals shadow).
    desugar lifts them to top-level `<Name>__$ctor` / `__$dtor`.
    A CTOR/DTOR IS A METHOD WITH RESTRICTIONS — like an operator. EVERY syntax a method
    admits, a hook admits: a body-inline definition, a bodyless forward DECLARATION, a
    definition in any RE-OPEN, and the EXTERNAL out-of-line form (`C:_()`, `A:B:~()`) in
    any scope the class is declared (see THE EXTERNAL FORM). The restrictions are only:
    no author params, no return type, class-only (a namespace has no lifecycle — the
    qualified spelling is rejected in relocation, not just the bare one in the parser),
    and a ctor is never `virtual`.
    THE CONTRACT IS PER CLASS, NOT PER BODY (see THE LIFECYCLE IS THE UNION OF EVERY
    OPENING, below): hooks are optional but must PAIR, a FORWARD declaration (`_();` — a
    bodyless kFunctionDecl member) must be DEFINED in SOME opening (not necessarily the
    one that declared it), and a DUPLICATE definition is one across ALL openings. All
    three are enforced in registerClassBody, the only place the whole class is visible;
    the parser reads one body at a time and enforces NONE of them (it keeps no hook state
    at all). The kSlid type carries has_ctor/has_dtor (the explicit symbol exists) vs
    needs_ctor/needs_dtor (TRANSITIVE).
  * CALL-IF-NEEDED + ITANIUM RECURSIVE DESCENT (complete-method model): a trivial
    class emits no method and no call. needs_ctor/needs_dtor is transitive over the
    field graph (resolve fixpoint after all classes register — a by-value field whose
    class needs hooks propagates up). Each NON-TRIVIAL class emits a COMPLETE
    `@<Class>__$ctor(self)` / `@<Class>__$dtor(self)` (materialized once per class in
    codegen run()): the ctor constructs each hook-carrying field (base at slot 0 first,
    declaration order) then stamps the vtable then runs the user body; the dtor stamps,
    runs the user body, then tears fields down in REVERSE. The user's `_()` / `~()`
    statements are emitted separately as `@<Class>__$ctor__impl` / `__$dtor__impl` and
    called by the complete method only when has_ctor / has_dtor (a synthesized-only
    class runs just the field hooks + stamp). CONSTRUCTION / DESTRUCTION SITES do
    value-init (the field-value stores) inline, then DISPATCH: emitConstructHooks /
    emitDestructHooks route a class object to its complete method (`call @<Class>__$ctor`)
    and inline aggregate (array / tuple) walks down to a class leaf — a tuple/array has
    no method, so its hook-carrying slots/elements construct in place. The dispatch is
    gated on typeNeedsHook (same predicate as the run() synthesis), so a trivial class
    reached at any site emits nothing. The unused-local sweep exempts a hook-bearing
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


CLASS-OPERATOR CHAINS — MINIMUM TEMPORARIES (landed 2026-07-12; classify + desugar)
canon test/class/evaluate.sl (blocks J-P). Plan: plan-evaluate.txt.

  * AN OPERATION ON AN AGGREGATE OF CLASSES IS THE OPERATION BY SLOT (landed 2026-07-13).
    A class operator over a tuple / array of classes needs NO class-specific aggregate code:
    classify's SLOT-WISE EXPLODE (readme.txt) rewrites `(a,b) + (c,d)` into the tuple literal
    `(a+c, b+d)` BEFORE any of the machinery below runs, so each element is an ordinary class
    binary — stamped, chained, lowered, exactly like a scalar one. Aug-assign explodes into
    per-slot STATEMENTS so a class slot reaches its own `op+=`.
    THIS IS WHY IT COULD NOT HAVE LIVED IN CODEGEN: a class operator needs an ADDRESS (its
    `^self` receiver, its sret destination), and codegen's aggregate walkers worked on SSA
    VALUES (extractvalue / insertvalue), where a slot has none. They could only emit numeric
    instructions — so a class-bearing aggregate emitted `add { i32 } %a, %b`, INVALID IR that
    slidsc accepted (widen::commonType succeeds for two identical class types). Both walkers
    are deleted. Canon evaluate.sl block Z; the ORDER (a chain landing in a slot must be
    constructed before it is written into) is block Z7.
  * THE SPLIT: classify RESOLVES a class operator and STAMPS it; DESUGAR LOWERS it. classify
    leaves the kBinaryExpr in place (class_op_chain) carrying five resolved operator entry
    ids — the 2-arg `op<OP>(lhs,rhs)`, the fuse `op<OP>=(rhs)`, the two seeds `op=(lhs)` /
    `op=(rhs)`, and `op<--(Self^)` for the move — plus, as children[2], the result class's
    DEFAULT field-init tuple (an accumulator's construction value, built by the ONE
    construction funnel). WHY desugar owns the lowering: the ELIDE (letting the destination
    BE the accumulator) needs the chain AND its destination together, and only a whole
    statement carries both. classify::stampClassBinary replaced tryLowerClassBinary.
  * THE ARITY-1 UNARY IS A CHAIN NODE TOO (2026-07-13). `-a` PRODUCES a whole class value,
    exactly as a binary does, so it takes the same road: classify::stampClassUnary stamps the
    kUnaryExpr (class_op_chain, children [operand, default field tuple], op_un_eid = the 1-arg
    `op<OP>(Self^)`, op_move_eid = op<--), and desugar's lowering places the accumulator. It
    runs ONE operator — it writes the accumulator's whole value — so there is no seed and
    nothing to fuse it with. Consequences, all of them inherited rather than re-implemented:
    fresh storage IS the accumulator (a decl, a return via NRVO — zero temps); a LIVE target
    takes one temp MOVED in through op<--; a unary at a chain's HEAD collapses into the
    accumulator exactly as a head construction does (`-a + b` = `acc.op-(a); acc.op+=(b)`);
    anywhere else it is an ordinary operand with its own statement-scoped temp. The spine walk
    STOPS at a unary (collectChainSpine): continuing into its operand would put `-(a+b)`'s
    inner chain on the spine and then apply the unary to the accumulator — `acc.op-(acc)`,
    reading the accumulator while writing it, the same self-alias the un-fusable rule refuses.
    Dispatch is on the OPERAND's class ALONE: the old arm let the CONTEXT (the assignment
    target) pick the result class, the same precedence inversion that killed the target-keyed
    binary fuse; a cross-class destination now converts through the binding funnel like any
    other. lowerClassOperatorTemp — the classify-side `_$optmp` + class_conversion lift this
    replaced, and the last elide the chain landing left behind — is DELETED. Canon evaluate.sl
    block U (class Neg).
  * WHERE THE ACCUMULATOR LIVES = the declarator funnel's existing question, "is this RAW
    STORAGE being constructed, or a LIVE object being assigned?" (desugar::expandOpChainStmt):
      - a DECLARATION of the chain's exact class -> the declared local IS the accumulator.
        ZERO temps. No aliasing guard needed: a variable cannot appear in its own initializer
        (`Acc c = c + 1` is already "Use of uninitialized variable"), so a chain can never
        read the storage it builds into. The exact-type check is the only guard.
      - a RETURN of the exact class -> build into a local and return it; analyzeNrvo aliases
        that local to %sret.in, so the CALLER's storage is the accumulator. Zero temps.
      - a LIVE target (assign / store through an lvalue) -> a temp accumulator, MOVED in via
        a CALL to op_move_eid, all inside a BLOCK so the temp dies at the SEMICOLON. A live
        target can NEVER be the accumulator: that means re-running its ctor, and the op= seed
        would land on its OLD value.
      - no destination at all (a call arg, a nested operand) -> a `_$optmp` lifted into the
        statement's `pre` by liftSretCallExprs, statement-scoped by its existing wrap.
  * THE LADDER (desugar::lowerOpChain, walking the left spine innermost-first):
      1. a fresh `Class` / `Class()` / `Class(args)` at the HEAD COLLAPSES INTO the
         accumulator — constructed with those args, never materialized as an operand.
         THE COLLAPSE IS AN OPTIMIZATION, SO IT CAN BE DECLINED. It saves one temp, but an
         accumulator can only be ADVANCED by an operator that MUTATES it with one argument
         (rules 2/3), and a class may have only the 2-arg `op<OP>`. Classify then DECLINES the
         collapse (stampClassBinary: `if (!viable && bin_id >= 0)`) and the construction becomes
         an ORDINARY rvalue operand of rule 4 — one temp, dead at the semicolon, exactly what
         the same construction on the RIGHT has always cost. It used to give up instead and
         report the operator UNDEFINED, though it is defined and the identical rvalue dispatched
         fine as a CALL head (`mk() + a`) or as the right operand (`a + C(7)`).
         THE ROLE IS DECIDED IN CLASSIFY AND STAMPED ON THE NODE (`op_collapse_head`); desugar
         OBEYS it. It used to RE-DERIVE the role here (lhs.is_construction), which is precisely
         why classify could not decline: the two ends would have disagreed, and desugar would
         have collapsed anyway and reached for an op_aug_eid of -1.
      2. first operand on a DEFAULT accumulator: prefer `op=` (cheaper on an empty object),
         fall back to `op<OP>=`. (A default head always HAS an `op=` — the synthesized copy —
         so it always collapses; rule 1's decline only ever fires on a head built WITH args.)
      3. on an accumulator built WITH ARGS: `op<OP>=` ONLY — an op= would discard them.
      4. a real operand PAIR at the head: the 2-arg `op<OP>(x,y)` in one call, else DECOMPOSE
         to `acc.op=(x); acc.op<OP>=(y)`.
      5. every later operand FUSES: `acc.op<OP>=(operand)` — with ITS OWN operator, so a
         mixed chain (`a + b - c`) really subtracts.
      6. a continuation with NO `op<OP>=` cannot fuse (`acc.op+(acc,c)` reads the accumulator
         while writing it), so it starts a FRESH BUFFER: one extra object per un-fusable step,
         and the accumulator the destination becomes is the LAST one. [DEFERRED: ping-pong —
         two alternating buffers would need none of them.]
    RECORDED CONSEQUENCE: the seed does not consult the head operator symbol, so
    `Vec v = Vec - a - b` is `v.op=(a); v -= b` = `a - b`. Accepted (canon 6).
  * EVERY operator a chain runs — seed, fuse, 2-arg head, AND the move — is emitted through
    ONE helper (desugar::makeOpCall, from the entry id classify resolved), so none of them
    can reach codegen as a bare transfer node. That matters: a transfer synthesized in DESUGAR
    never passes through classify's operator dispatch, so naming the operator is the only way
    to guarantee it runs (see THE TRANSFER INVARIANT above — a bare move node relied on a
    codegen gate that blitted for a hook-less class).
  * A DERIVED VALUE BINDS TO A BASE REFERENCE PARAM (fixed 2026-07-12; argConvertCost's
    auto-ref arm + the shared checkArgAssign + emitCall's class-by-address test). An INHERITED
    operator's params are typed as the BASE, so a derived operand had to bind a `Derived` value
    to a `BaseOp^` param. That is an UPCAST — the base IS the derived's slot-0 sub-object, so
    the address is identical and NOTHING is sliced — but the auto-ref (value -> reference)
    convenience only accepted an exact class or a per-leaf widen, and returned -1 otherwise.
    So EVERY user-written base operator was DEAD from a derived operand: `Derived + Derived`
    reported "Operator '+' is not defined on class 'Derived'", and `Derived += Derived` fell
    through the aug-assign hole (below) into INVALID IR. Now the auto-ref arm grants rung 2 —
    the same single implicit cast the POINTER arm already gave an explicit `^derived` ->
    `Base^` (ptrBaseUpcastOk). A genuine VALUE assignment (`Base b = d;`) WOULD slice and is
    still rejected by checkSlidAssign; only the by-address arg path is affected. Pinned by
    evaluate.sl Q1.
  * A COMPOUND OPERATOR WITH NO MATCH IS REJECTED (fixed 2026-07-12). `a += b` on a class with
    no `op+=` used to fall through the aug-assign arm into the NUMERIC path — where commonType
    SUCCEEDS for two identical class types — and emit a struct `add`: INVALID IR. The
    kBinaryExpr arm has had the no-viable-operator guard since the class-binary work; the
    kAugAssignStmt arm never got it. Now: "Operator '+=' is not defined on class 'X'." Pinned
    by evaluate.sl's neg_aug_no_op.
  * COUNTING LIVES IN evaluate.sl, NOT operator.sl. operator.sl's OpDefs/Sum have no
    ctor/dtor, so a fuse and a no-fuse print identically there — which is exactly how the old
    target-keyed chain fuse survived for months with green goldens. evaluate.sl's Acc / Str /
    Buf / Triv / Sw / Neg PRINT their ctor/dtor (or their operators), and the golden IS the
    assertion. The blocks:
      J  the baseline — 12 objects -> 7.
      K  the only-op+= ladder (canon 51-56; class Str, no 2-arg op+).
      L  the TRIVIAL bucket — a hook-less class whose operators print (the transfer gates).
      M  the destinations + shapes: a call arg; a store through a deref / a class field / an
         array element; mixed operators (`a + b - c` really subtracts); a construction as a
         NON-head operand; a parenthesized sub-chain; a chain in a loop body.
      N  the UN-FUSABLE chain (class Buf: a 2-arg op+, no op+=) — the buffers AND their
         statement lifetime.
      O  hook-less transfers through all three gates: swap, tuple copy, tuple move, array copy.
      P  DESTRUCTURE — values right, shape NOT minimal (a `_$dsrc` spill; see todo).
      Q  where the operator COMES FROM: INHERITED, VIRTUAL (dispatched through the vtable),
         used inside a METHOD body, used inside a CTOR and a DTOR body.
      R  the other operator KINDS + the last destinations: a SHIFT chain (fuses via op<<=), a
         LOGICAL binary, a GLOBAL target, a CROSS-CLASS destination (the funnel converts), and
         chains in a switch-case and a do-while body.
      S  the last positions: for-long and for-ranged bodies; a HEAP target (`p^ = a + b`) and a
         heap-deref OPERAND; destructure into DECLARING (fresh) class slots.
      T  the BLAST RADIUS of "a derived value binds to a base reference" — an EXACT derived
         param must still beat the rung-2 upcast (T1), a base-only overload IS reachable from a
         bare derived value (T2), DYNAMIC DISPATCH survives the upcast (T3), and a hook-less
         class with a POINTER field moves through its operator and leaves a husk (T4). T1 and
         T3 are the ones that would fail SILENTLY — the wrong overload, or a static call where
         a virtual one was meant — so nothing else in the suite would notice them.
      U  the ARITY-1 UNARY (class Neg), now on the binary's road: a fresh decl and an NRVO'd
         return cost ZERO temps (U1/U6), a live target one temp moved in through op<-- (U2), a
         unary at a chain HEAD collapses into the accumulator (U3), and elsewhere it is an
         ordinary operand with its own statement-scoped temp (U4 in a chain, U5 as a call arg,
         U7 over a sub-chain).
    NEGATIVE: neg_aug_no_op — `+=` on a class with no op+= (the aug-assign guard).


FOR-CLASS — ITERATING A CLASS BY ITS PROTOCOL (landed; resolve understandForClass)

  * A class is iterable in `for (var : container)` if it defines ONE of two protocols
    (found by NAME across the class + its bases, classAndBaseFrames): size/op[] —
    `size()` arity 0, `op[](i)` arity 1 RETURNING A REFERENCE to the element — or
    begin/end/next — arity 0/0/1, all returning (and next taking) the SAME type. A
    malformed set is not an error on its own; it just can't form the protocol, and a
    malformed set is IGNORED when the other protocol is usable.
  * SELECTION (option D): both protocols defined → the loop-var type must be EXPLICIT
    (an inferred one is an error), and its shape picks — a VALUE loop var selects
    size/op[], a REFERENCE selects begin/end/next. Exactly one defined → it is used
    (it serves both loop-var shapes). Neither → "not iterable". A typeless loop var is
    inferred: a primitive element binds by value, a class element by reference.
  * LOWERED AT RESOLVE, not retagged for desugar (the array/tuple model). The lowering
    emits METHOD CALLS (begin/size/next/op[]), and a call minted in desugar is never
    classified — so understandForClass rebuilds the node as a kForLongStmt and
    re-resolves it, letting classify's ordinary pass infer the calls. Same reason
    enum-for lowers at resolve. Nothing downstream needs for-class code.
  * FIVE DESUGAR SHAPES: begin/end/next returning a VALUE [loop var IS the iterator,
    `var = c.begin()`, advance `var = c.next(var)`]; returning a REFERENCE by-value
    [hidden `_$fc_ref` iterator + `var = _$fc_ref^`] or by-ref [loop var IS the ref];
    size/op[] by-value [`var = c.op[](i)^`] or by-ref [`ref = c.op[](i)`, the op[]
    reference bound directly — NOT `^c[i]`, which codegen's addr-of rejects]. op[] is
    called as a method node, since `.op[](i)` is not surface syntax and `c[i]` sugar
    always derefs.
  * CONTAINER LIFETIME: a re-readable lvalue (a var, `ptr^`, an index) is cloned per
    method call; an RVALUE (`C(..)`, `fn()`) is SPILLED to a class temp that WRAPS the
    loop in a block scope — a for-long varlist local is NOT destructed at loop scope,
    a block local IS — so the temp is built once and destructed at loop exit. Synth
    locals are token-suffixed so NESTED for-class loops never collide. Loop-var
    widening/truncation, the unused-loop-var sweep, and break/continue all fall out of
    the kForLongStmt. Canon test/flow/forclass.sl.


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
  * EMPTY CLASS MINIMUM SIZE — an instantiable class with NO fields lowers to the
    1-byte struct `{ i8 }`, not `{  }` (0 bytes), so distinct instances occupy
    distinct storage (C++'s empty-class rule; `^a == ^b` is observable). The single
    fix is in llvmForRef (the zero-slot kSlid arm); it propagates everywhere layout
    is read: `alloca { i8 }`, array element stride 1 (`[2 x { i8 }]`, so
    `^c[0] != ^c[1]`), tuple slots at offsets 0 and 1 (`{ { i8 }, { i8 } }`), and
    `__$sizeof` -> 1 (the GEP-null over the padded struct). The padding byte is never
    named (the class has no fields), so field-init / `.field` are unaffected. Only a
    class is padded — an empty tuple is not. Without it a 0-byte empty class makes
    array/`new[]` elements and tuple slots ALIAS (stride 0 / both at offset 0); a
    plain pair of stack locals only differed by incidental frame layout. Canon
    test/class/empty.sl.
  * NEW T / NEW T(args) — a class is sized by `call @<Name>__$sizeof()` (not the
    typeByteSize literal). `new T(args)`: grammar parses the trailing `(args)` onto
    kNewExpr children[2] (distinct from the leading `new(addr)` placement and `[n]`);
    classify routes it through constructClass (the same field-init tuple as a class
    var-decl); codegen mallocs, then constructs THROUGH THE DECLARATOR FUNNEL —
    emitConstructAt(register_dtor=false, scope=nullptr) fills at the pointer and
    dispatches the class's complete ctor `@<Name>__$ctor` (an absent init default-
    constructs; delete owns the dtor). PLACEMENT `new(addr) T(args)` reuses the same
    construct at the buffer address (no malloc).
  * NEW T[n] / NEW T[k](init) (the new[] COOKIE) — a class array WITHOUT an initializer
    uniformly default-inits each element (broadcast the default value into the slot,
    finalize per element via emitConstructed); a HOOK class additionally prepends an
    8-byte count COOKIE (the returned pointer is malloc+8) so delete can loop the dtor.
    The cookie gates on needs; the broadcast does not (a trivial class still has field
    defaults). WITH a size-matched initializer `new T[k](a, b, ...)` (k a LITERAL),
    classify types children[2] as the `T[k]` array (classifyArrayFromTuple — the same
    shape check + per-element construction as the stack `T arr[k](...)` form) and codegen
    builds the WHOLE array in ONE emitConstructAt: the array<->tuple bridge distributes it
    element-by-element and emitConstructHooks runs each element's ctor. No new init
    semantics — matched-size is required (a runtime count or a mismatched shape rejects).
    A primitive array stays a plain malloc (a size-matched initializer fills the slots via
    the same whole-array emitConstructAt).
  * DELETE — operand is ANY pointer expression. For an LVALUE (variable / field /
    array element / tuple slot / deref) codegen takes its address ONCE (emitLvalueAddr
    — so `delete arr[bump()]` runs the index once), frees, and stores null BACK
    through that address; an RVALUE (a call return / op result) is freed with no
    null-back (a temporary has no storage). The free itself: single (T^) —
    null-guarded dtor (free(null) is safe; the dtor on null derefs), then free; array
    (T[]) of a hook class — read the count at ptr-8, run the dtor on each element in
    REVERSE, free ptr-8; a primitive / trivial-class pointer is a plain free (gated on
    typeNeedsHook(pointee)). This makes a class's dtor able to `delete` a field it owns
    (RAII).
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
    store target (field WRITES, previously only ctor-body `self.field`).


CLASSES: AS A NAMESPACE + LOCAL (defined in a function body) (landed; spans stages)

  * A CLASS IS ALSO A NAMESPACE. Every class gets a `kClass` ENTRY (a new
    EntryKind) carrying an ns_frame_id (its member set) AND its kSlid as
    slids_type (so the name is a type too). Its body holds member DEFINITIONS —
    aliases, consts, enums, methods, nested classes, AND nested namespaces (a free
    function in a class body becomes a method; only a bare runtime statement is
    rejected). The class is itself a member of its enclosing scope — see NAMESPACE
    ↔ CLASS below. Qualify members by the class name:
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
    local-class pre-pass over EACH scope's statements before resolving them — and
    before the const pre-pass, so a `const LocalClass c` decl's type resolves
    (resolveFunctionBody and resolveStmtList both register classes first) — so a
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
    `Outer:Inner:Innerger`, any depth). registerScopeNames recurses into the host's
    frame (registerClassName name-only with member_of = the host frame; def_id = host
    frame, so `Outer:Inner` is a distinct identity); resolveQualifiedType accepts a kClass
    leaf and returns the HANDLE via an out-param (NEVER a spelling round-trip — a
    class's def_id can't survive one). A hoisted class is NOT bound to a host
    object: it sees the host's namespace members (`Inner`'s body reads `Outerger`
    bare, `Outer:Outerger` qualified) but NOT the host's fields (a bare host field
    is Unresolved). classify recurses into hoisted ctor/dtor (classifyScope).
  * SCOPE-AWARE MEMBER RESOLUTION (name-then-resolve, frames open) — NOT a leniency.
    A type name resolves via resolveName (open-ns chain + lexical-with-owner<0), not
    a frame-blind any-live-entry lookup. For that to be correct, member TYPES must
    resolve with the enclosing frame OPEN: the NAME (registerScopeNames), TYPES
    (resolveScopeTypes — incl. field types via registerClassBody), and BODY
    (resolveScopeBodies) phases all RECURSE through the scope tree, so the enclosing
    frame chain is naturally on the stack at every member-type / body resolution (names
    were registered first — all names before any type — so forward refs resolve).
    Result: a member type resolves
    bare only where its frame is open (inside the host); a bare member type at file
    scope FAILS — "'Inner' needs a namespace qualifier" when it IS a member-type
    elsewhere (namespaceMemberTypeExists — class/alias/enum, NOT a const/function),
    else "Unknown type". (resolveName is the sole resolution path — the legacy
    frame-blind any-live-entry lookup has been removed.)
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
    name. Grammar parses it like any function and injects an implicit RECEIVER param
    `_$recv` (Class^, the object's address) at params[0] — the node built by the
    shared factory `parse::makeReceiverParam`, the ONE construction of the receiver
    param, reused by the ctor/dtor form (grammar) and the out-of-line `Class:method`
    relocation (resolve). (The spec `self` is the
    OBJECT — `_$recv^`; the internal param is the pointer, named distinctly so it
    never collides with the `self` keyword.) The body is a FULL function body (local
    consts/classes, unused-local sweep, nested functions, return checks), with bare
    field names rewritten to the spec `self.field` for READS and WRITES (buildSelfField
    — one place mints `_$recv^.field`, shared by the kIdentExpr read path and the
    assignment-LHS write path; a bare `x_ = v` becomes a field store, not a phantom
    local). resolve/classify reach a method through the SAME ctor/dtor sites (the
    forEachHoistedClass walker, now filtered on kFunctionDef). The method entry's
    param_types hold the FULL list (`_$recv` at [0], aligned with the node's params so
    the resolved-type write-back stays index-correct); it lifts to
    `<Class>__method(_$recv, ...)`. A call `obj.method(args)` parses to kMethodCallStmt
    (children[0] = receiver); classify resolves the method via the receiver's class
    frame (classEntryForType + findMemberDeclared — the shared member lookup),
    type-checks args against param_types[1..], and threads the method entry id;
    desugar lowers it to a normal call of the lifted symbol minted from the method's
    OWN DEFINING class (not the receiver's, so an inherited call will name the base),
    with the receiver's address prepended as `_$recv` (for `ptr^.m()` the receiver IS
    the pointer — an addr-of of a deref is not a codegen lvalue). A bare call resolving
    to a method errors ("Method 'm' must be called on an object.") — call it on the
    receiver or via `self`. LANDED since: the EXPRESSION form (`x = obj.m()`, lowered
    to a value kCallExpr); SIBLING calls via `self` (`self.m()`); and a method signature
    naming its OWN class (`Self^ m(Self^)` — the placeholder ClassInfo is emplaced
    before member registration so the type is known). A PAREN-LESS callable used as a
    VALUE is an ERROR, never a fabricated call — a function/method is only ever called
    with `()`. classify rejects a bare method name `obj.m` (kFieldExpr with no args,
    searching the receiver's class + base frames) and a bare FUNCTION name `x = fn`
    (kIdentExpr resolving to a kFunction) alike, with `Function call is missing
    parameter list '()'.`. (Before: `obj.m` was silently rewritten to a call and
    `x = fn` crashed codegen — the kIdentExpr-not-in-SymTab assert.) A paren-less FUNCTION
    name as a STATEMENT gets the SAME message (`fn;` -> `Function call is missing parameter
    list '()'.`, in resolveCallTarget); `cls.method;` is `Expected '='` at grammar. (A bare
    VALUE name as a statement is NOT an error — it is a discarded read; see BARE CLASS NAME
    above.) Canon test/class/method.sl + test/function/call.sl. The `self` KEYWORD is an
    address-aliased LOCAL of the class type whose storage IS `_$recv`'s target:
    `self`, `self.field`, `self.m()`, and `^self` (= `_$recv`, its own address) all
    flow through ordinary local machinery — resolve registers it in
    resolveFunctionBody (like a param, not in body_locals); codegen binds its SymTab
    address to `_$recv^` once at the prologue (self_entry_id on the fn node). Bare
    `x_ += 1` field compound-writes resolve to the field too (the kAugAssignStmt
    rewrite). A CONSTRUCTION receiver / nested arg in a method call (`Class(2).m()`)
    is lifted to a `_$cret` temp; WHERE its dtor fires depends on position. A
    discarded kCallStmt/kExprStmt block-wraps the temp with the statement (SEMICOLON).
    A nested arg / receiver temp in a var-decl / assign / return rhs is folded into a
    kSeqExpr wrapping the rhs, so it too dies at the STATEMENT (liftSretCallList,
    2026-07-11) — for EVERY rhs value form since 2026-07-13. It used to be SCALAR-only:
    a CLASS-valued rhs is built IN PLACE by the statement's sret fast paths, and those
    matched on the RAW call node, so a seq around it would hide the call and force an extra
    copy — the wrap was declined and the arg temp fell back to ENCLOSING-scope lifetime.
    Codegen now OPENS the seq at those three statements (openRhsSeq / closeRhsSeq): it
    constructs the temps, hands the sret path the seq's VALUE child, then destroys them —
    so construction in place and statement-scoped temps hold AT ONCE. Pinned by evaluate.sl
    case 7; block P (destructure) got it for free, its `_$dsrc` spill being a class-bearing
    tuple that had leaked its chain accumulators for exactly the same reason. A
    CONDITION (if/while/for/switch) lifts into the condition's PPID seq (kSeqExpr),
    constructed at phrase entry and DESTROYED after the value — so a loop receiver is
    rebuilt+destroyed each iteration and an if-temp dies before the body. ONE shared
    emitSeqEffects emits a seq's effect children whether the seq sits in an EXPRESSION or
    at a statement rhs, so a temp gets the same lifetime either way; desugar's
    lowerPhraseSlot lifts constructions for conditions (lift_constructions). A `&&`/`||` short-circuit RHS lifts into its OWN sub-seq
    (lowerPhraseSlot recurses per short-circuit operand), so a skipped branch runs no
    ctor/dtor; the unconditional LHS lifts into the condition pre. Still
    todo.txt: a construction in a store-target / move operand (rejected). Detail:
    [[project_self_and_method_calls]].
  * BARE-FIELD REWRITE reaches MORE expression contexts. Besides reads / `=` writes /
    compound writes, the `bare field -> self.field` rewrite now fires for a `##type`
    OPERAND (`##type(field)` -> the field's type) and an ADDRESS-OF operand (`^field`
    -> `^self.field`, using the `self` keyword so the addr-of walk descends to a
    resolvable base). So a bare field works as a value, a store target, a `##type`
    operand, and under `^`. Canon test/class/field.sl.
  * METHOD / FUNCTION PARITY — methods get OVERLOADING + DEFAULT PARAMS + INFER-PARAM-
    TYPE-FROM-DEFAULT (the three callable features free functions already had) through
    ONE shared overload engine. The RANKING is factored into a single pure core
    rankOverload(cands, args, recv_offset) → {best, tied[]} (a per-arg convert RUNG —
    exact 0 / alias 1 / cast 2 / smallest same-sign 3-5 / cross-sign 6-8 widen — a
    candidate scored by the MAX rung over its args, lowest score wins; `tied` retains
    EVERY candidate at the winning score). pickOverload
    wraps it, keeping "No matching overload" (nothing viable) and routing a genuine tie
    (`tied.size() > 1`) through the shared reportAmbiguity, which emits "Ambiguous call"
    PLUS one note per tied candidate ("candidate '<sig>' declared here", anchored at its
    def_tok) so the author sees the exact conflicting declarations. Used by BOTH
    classifyCall (recv_offset 0) and inferMethodCall (recv_offset 1 — `_$recv` is held
    out of ranking + the arity range). The assignment-operator path findClassOperator
    gathers its base-chain candidates (most-derived frame shadows bases) then ranks them
    through the SAME rankOverload/reportAmbiguity (recv_offset 1, the lone `rhs` operand);
    a tie is reported with candidate notes and returns the -2 "ambiguous, already
    reported" sentinel, -1 stays "none" (caller errors). So every function / method /
    operator match shares one ranker and one ambiguity diagnostic.
    THE OVERLOAD SET ITSELF IS CHECKED WHERE IT IS DECLARED (2026-07-13,
    checkOverloadDefaultCollisions — classify, right after the signature pre-pass, the
    point where every param type and num_required are final). A DEFAULT parameter makes a
    candidate's arity a RANGE, so two overloads can admit the same argument count; when
    they ALSO agree on the parameter types up to that count, no call at that arity could
    ever tell them apart — so the PAIR is the error, not the call: "Ambiguous overloads of
    'f': a call with 1 argument matches both." (`f(int)` beside `f(int, int = 0)`: either
    f(i) is ambiguous, making f(int) uncallable, or it picks f(int), making b's default
    unreachable — both readings are broken, so neither is taken.) It compares ENTRIES, so
    a METHOD set gets the identical rule with no second implementation — the `_$recv` sits
    in both prefixes and cancels, so the reported argument count reads as the user writes
    the call, and VIRTUAL changes nothing (the set is checked before any dispatch
    question). An identical-param_types pair is SKIPPED: that is a forward decl + its
    definition (separate method entries), owned by the duplicate-definition check above.
    Canon overload_cls.sl (am1 / am0), virtual.sl (Namb), overload_fn.sl (amb_one /
    amb_zero).
    resolve allows a method OVERLOAD SET in a class frame (a same-name method is an
    overload; a collision with a non-function member is still a dup); a NAMESPACE
    function stays single-definition. classifyScopeSignatures runs classifyFunctionSig-
    nature over EVERY member (defaults + num_required + infer-from-default) BEFORE any
    body types; fillDefaults is receiver-aligned. Method-only signature checks (a type-
    less param needs a default; a required param may not follow an optional) run in
    classifyFunctionSignature; two same-signature method DEFINITIONS are a "Duplicate
    definition". A method FORWARD DECLARATION is satisfied by a same-signature definition
    in the EXACT scope (orphan check matches owner_ns_frame + parent_frame_id +
    param_types; inferMethodCall gathers only DEFINED candidates). Distinct overloads get
    distinct symbols via methodSymbol (mirrors functionSymbol's entry-id mangle). A
    CONSTRUCTOR has no overload analog — `_()` is nullary (a hook over tuple-initialized
    fields), not a signature-bearing callable. Canon test/class/overload_cls.sl +
    method.sl; detail plan-method-parity.txt; [[project_method_function_parity]].
  * NAME COLLISIONS + TYPE-NAME DIAGNOSTICS. A class name collides with ANY
    same-name entry (another class, an alias / enum / namespace, a const, a
    function) — reportNameCollision carets the source-LATER declaration as the
    duplicate (registration order need not match source order, so it compares
    positions). requireKnownType, when a type name resolves to a non-type entry,
    reports "'X' is a namespace / function / constant / variable, not a type"
    instead of a blunt "Unknown type" (the precise form fires where the entry is
    already registered — e.g. a body var decl; a class FIELD validates before
    file-scope functions/consts register, so those stay "Unknown type").


NAMESPACE ↔ CLASS — ONE SCOPE ABSTRACTION (landed; spans parse / desugar / classify / resolve)

  A namespace, a class body, and PROGRAM scope are the same construct: a brace body
  holding member definitions. A class is a namespace with a field tuple + methods; a
  namespace is a class with neither (and non-instantiable); program scope is the
  implicit global namespace bounded by EOF instead of braces. So they nest in each
  other freely: a class in a namespace (`Ns:Class`), a namespace in a class
  (`Class:Ns:member`), to ANY depth (`Ns:Class:Ns2:Inner`), each registered by the
  SAME code as the one-level case.

  * PARSE — one member dispatch. parseDefinitionMember(in_class, recv_type) is the
    universal definition-member parser (const / alias / enum / nested class / nested
    namespace / function), shared by a namespace body, a class body, and program
    scope (parseProgram → parseNamespaceMember → parseDefinitionMember). Only two
    bits vary, both intrinsic: a function becomes a METHOD in a class (receiver
    `_$recv` injected); the function fallback is PERMISSIVE outside a class (a
    namespace/program member that isn't matched is a function def OR `;`-decl, so
    forward declarations parse — looksLikeFunctionDef is a statement-context
    disambiguator that wrongly rejects `name();`, so it gates ONLY in a class body).
    ctor/dtor are method-shaped, legal only in a class. The shared statement-body
    loop is parseStmtsThroughRBrace.
  * DESUGAR — one flatten. flattenScope recurses every scope uniformly: a namespace
    hoists its direct functions, a class lifts its methods + `__$ctor`/`__$dtor`
    hooks, both recurse — so a scope nested in a scope, any depth, flattens by the
    same walk (replaced collectClassDefs + collectAllNamespaces + the two lift loops).
  * CLASSIFY — one walk. classifyScope types a scope's member bodies (functions +
    const inits) and recurses into nested namespaces AND classes through itself;
    run() calls it once on the program (replaced classifyNamespace +
    classifyClassMemberBodies + their cross-arms).
  * RESOLVE — FULLY UNIFIED (NAME + TYPES + BODY phases). Three recursive routines over
    a scope, each with an isClass config; the parallel namespace/class pipelines and all
    cross-arms are GONE. Run() does: global NAME -> global TYPES -> cycle -> needs-fixpoint
    -> function entries -> BODY.
    - NAME phase — registerScopeNames(node, frame, classes): one recursive walk
      registers every member NAME / entry (const/function/method/alias/enum) with
      PROVISIONAL types, opens nested namespace frames, and registers nested class NAMES
      (slotless kSlid + placeholder ClassInfo via name-only registerClassName), recursing
      through nested namespaces AND classes. NO declared type is resolved here. Every
      class NODE (file / namespace-nested / hoisted, any depth) is collected so cycle +
      the needs-fixpoint can sweep them after TYPES. Replaced registerClassMembers +
      registerNamespaceTree + registerMemberSignature + collectNamespaceClasses +
      registerNestedNamespaceClasses (all DELETED) and the registration arms.
    - TYPES phase — resolveScopeTypes(node, isClass): now that EVERY name exists, resolve
      EVERY declared type with the scope frame open — member alias TARGETS first, then a
      class's FIELD types (via the now frame-agnostic registerClassBody, attaching slots),
      then const + function/method SIGNATURE types — writing back to the entries, recursing
      into nested namespaces AND classes. Because it recurses with the frame stack open, a
      field / signature naming a hoisted member or an enclosing-scope sibling bare
      (`Ring { Ping(Pong^) Pong(Ping^) }`) resolves with NO frame-chain reopening (the old
      openOwnerChain workaround — needed only while a FLAT field loop ran outside the
      recursion — is DELETED). Resolving all types AFTER all names is what lets a member
      type name ANY class regardless of order (the old forward-ref bugs); a field may even
      be typed by a host member alias (aliases resolve first).
    - cycle + needs-fixpoint — checkClassByValueAcyclic over the collected classes + the
      transitive ctor/dtor fixpoint over tree.classes, after TYPES (field slots attached).
      checkClassCyclesAndNeeds bundles cycle + a local fixpoint for a function-body /
      local-namespace class set.
    - BODY phase — resolveScopeBodies(node, isClass): field-default exprs, const/enum-member
      inits, and every member function body, recursing.
    A side effect of the uniform vocabulary: a namespace now accepts MEMBER ALIASES
    (`Space { alias Int = int; }`) — it was a class-only member before. The isClass config
    (field tuple, method receiver self-binding, no-reopen) is the only split that stays.


SINGLE INHERITANCE (landed; spans grammar / resolve / classify; non-virtual)

  `Base : Derived(fields){ body }` declares Derived inheriting Base. THE WHOLE DESIGN
  is one idea: the base is an UNNAMED FIRST FIELD. grammar.parseClassDef recognizes the
  `Ident : Ident(...)` head and PREPENDS a synthetic `_$base : Base` param at slot 0, so
  Derived's layout is `{ Base, own... }`. Because the base is just a class-typed field,
  EVERYTHING about lifetime rides the existing machinery for free: layout/sizeof,
  per-field construction, ctor/dtor ORDER (Itanium: base first, derived dtor first —
  the base is the leading class-typed field), the needs-ctor/dtor fixpoint, and the
  by-value CYCLE check (an inheritance cycle is just a by-value field cycle —
  checkClassByValueAcyclic catches it; the resolve/classify base-chain walkers are
  bounded by class-count so a cyclic chain can't hang before that check fires).

  CONSTRUCTION IS FLAT. The base's fields splice in ahead of the derived's, so
  `Derived d = (b0, b1, own...)` fills base-then-derived. classifyClassInit walks a
  RUNNING FLAT INDEX: the `_$base` field consumes flatFieldWidth(base) initializers —
  0 for a data-less base (only consts/methods), 1 for a single-field base, N for a
  wider/transitive one — every other field consumes one; a leading base-class VALUE
  (`(Base(..), own...)`) is detected (same-type) and taken whole instead.

  MEMBER ACCESS. A derived OPENS its whole base chain — resolve.pushBaseChain pushes
  every ancestor's member frame onto open_ns_frames (deepest-first, so a nearer class
  shadows a farther one), making inherited STATICS (const/alias/enum) resolve BARE.
  Inherited FIELDS resolve bare too (resolve.baseFieldDepth rewrites a bare base field
  to `self._$base...(_$base).field`), and inherited METHODS via classify.inferMethodCall
  gathering the receiver's class frame + base chain (first frame with the name wins, so
  a derived same-name member HIDES the base's overload set — C++ name hiding); a base
  method runs on the derived receiver (the base sub-object is at offset 0 = the same
  address). The `Base:` QUALIFIER reframes `self` to slot 0: tryResolveBaseQualifier
  rewrites `Base:self`/`Base:field`/`Base:method()` to `self._$base...X` (parse allows
  `self` as a `:`-segment; depth from baseClassDepth handles `GrandBase:`), but a
  `Base:STATIC` is NOT a field access — it's left for the normal qualified-name lookup,
  which is the disambiguation handle for a shadowed static.

  POINTER CASTS. derived->base is IMPLICIT, base->derived is EXPLICIT `<Derived^>`;
  both are offset-0 pointer no-ops (classify.ptrBaseUpcastOk on the assignment relation,
  ptrBaseCastOk on `<T^>`), backed by parse::classBaseType/classify.isTransitiveBase
  reading the slot-0 `_$base` marker. An implicit DOWNcast is rejected; so is a cast
  between UNRELATED or SIBLING classes (not on one chain — nothing to reinterpret).

  SHARED DECODE. The per-step "base of a class" and the whole "class + its base frames"
  walk live ONCE in parse:: — baseTypeOf reads the slot-0 `_$base` field, classBaseType
  and classAndBaseFrames iterate it under a class-count guard (a backstop only; a cyclic
  chain is the by-value error above). Every consumer delegates the `_$base` decode to
  these: resolve (pushBaseChain / baseClassDepth / baseFieldDepth) and classify
  (flatFieldWidth / isTransitiveBase / method-overload gathering). No site re-open-codes
  the sentinel. Companion query parse::classHasField backs the field-vs-static tests;
  resolve.frameHasFunction is just findMemberDeclared + a kind check.

  DEFERRED: a derived static shadowing a SAME-NAMED base static is bare-ambiguous
  (qualify to pick) — see todo "OPENED-SCOPE NAME AMBIGUITY". VIRTUAL classes are now
  landed and compose with inheritance (see VIRTUAL CLASSES below); re-open is landed too
  (see RE-OPENING CLASSES below). Canon test/class/inheritance.sl
  (Stages 1-5; positives incl. synthesized ctor/dtor, field shadowing, transitive sizeof +
  single heap derived; negatives: implicit downcast, unrelated/sibling cast, off-chain
  qualifier, cycle + direct self-inheritance, name hiding).


RE-OPENING CLASSES + THE EXTERNAL FORM (landed; spans grammar / resolve; non-virtual)

  A class may be RE-OPENED to add members after its primary definition, as a BLOCK
  (`Class() { ... }`) or via the EXTERNAL qualified form (`Class:member`). Every opening
  merges into ONE class — members are visible bare or qualified across ALL openings. The
  PRIMARY (field-bearing) definition must come first; the class's LAYOUT is the primary's
  kSlid, and a field-bearing re-open is rejected ("Duplicate definition of class 'X'; a
  re-open cannot add fields") — UNLESS the class is INCOMPLETE (its field tuple ends with a
  trailing `...`), which lets later same-scope re-opens APPEND fields (see INCOMPLETE CLASSES
  below). Re-open is a SAME-SCOPE construct — all openings live in the
  scope where the class is declared. The CROSS-SCOPE / run-time-scope variant (re-opening a
  class from a scope that isn't its own) is a SEPARATE, not-yet-landed feature — REFINEMENTS:
  a scoped zero-field derived class ($T) that USURPS the base's name in the scope and only
  LOOKS like re-opening (rides the landed inheritance + a free offset-0 cast; motivated by
  giving a generic like `sort<T>` a method the element type lacks). Canon test/class/refine.sl.

  INCOMPLETE CLASSES (landed; single-file). A class whose field tuple ends with a trailing
  `...` is INCOMPLETE (grammar sets Node.is_incomplete; parseParamList, class field lists
  only, `...` must be LAST — v2 has no leading/interior form). The re-open field rule is then
  STATEFUL on ClassInfo.is_open: while OPEN a re-open APPENDS its fields (recorded as
  pending_fields POINTERS, kept owned by the re-open node so their default exprs resolve with
  the class frame open); a re-open whose tuple omits `...` CLOSES the class. registerClassBody
  interns the primary's own fields THEN the pending ones through ONE addField funnel, so the
  layout freezes in exactly one place — the slotless-handle-then-fill intern already deferred
  the freeze, and single-file every opening is seen before it. classify / desugar / codegen are
  UNTOUCHED: once interned it is an ordinary class (construction, field access, sizeof reuse
  existing paths; no privacy single-file; defaults optional on every field; an empty completed
  class is 1 byte). A COMPLETE class rejects a field-bearing re-open (above) and cannot be
  re-opened as incomplete. Canon test/class/incomplete.sl. Multi-file (future) rides the same
  model: a TU that never sees the close keeps the type open -> size deferred to link-time
  __$sizeof().

  BLOCK RE-OPEN. A same-name class with an EMPTY field list re-opens the existing one.
  registerClassName points the re-open node's resolved_entry_id at the PRIMARY's entry and
  marks it is_reopen; registerScopeNames recurses the opening's members into the SHARED frame
  (the persistent ns_frame_id that namespaces already reuse), so const / alias / enum / method
  / nested class / nested namespace all land in one member set. The field-body pass and the
  class-collection loops SKIP an is_reopen node (guarded by the flag), so the primary's fields
  / layout are never clobbered (its LIFECYCLE is a different story — see below: a re-open's
  hooks must reach the primary, and the skip is exactly what hid them); flattenScope lifts each
  opening's methods under `<Class>__method` for free. A re-open of a BASE is visible on a
  DERIVED instance (it rides the _$base chain); a nested class introduced by a re-open, and
  re-opened again, both work.

  THE LIFECYCLE IS THE UNION OF EVERY OPENING. A ctor/dtor is a member like any other, so the
  DECLARATION `_();` may sit in the primary with its DEFINITION in a re-open (canon reopen.sl
  `Forward`), the two halves may land in two SEPARATE re-opens (`Split`), or a re-open may add
  both with no declaration anywhere (`Late`). But an is_reopen node skips the class BODY passes,
  so its hooks are invisible to the primary's lifecycle scan — the same problem pending_fields
  solves for fields, solved the same way: the re-open branch of registerClassName points
  ClassInfo.pending_hooks at its IMPLICITLY-INVOKED members (still OWNED by the re-open node, so
  the body pass resolves them there), and registerClassBody scans the primary's own members PLUS
  pending_hooks. pending_hooks carries all FIVE — ctor, dtor, copy, move, swap (parse::
  isImplicitMember, THE single spelling of the list) — not just the two hooks: every per-CLASS
  question asked in registerClassBody has to see a member a re-open added, and the header-class
  ADD ban below asks about the other three. That scan is also where the WHOLE contract is enforced, classifying each hook as
  declaration-or-definition by node kind: PAIRING first, which GATES the must-be-defined
  obligation (so a lone `_();` reports the missing dtor once, not that plus "must be defined";
  and declaring the pair while defining one half names the missing DEFINITION rather than a
  phantom pairing violation), plus DUPLICATES — two definitions of one hook in two openings,
  which the parser's per-body check could not see and which reached codegen as two
  `@C__$ctor__impl` definitions, i.e. INVALID IR caught only by llc. Keeping the first def node
  gives that diagnostic the same "first defined here" note a duplicate METHOD already got — the
  asymmetry that gave the bug away. BEFORE this the scan saw only the primary, so a
  re-open's hook left has_ctor false: the hook was NEVER CALLED and its `__impl` was emitted
  DEAD — it compiled, linked, ran, and printed nothing. A GLOBAL group keeps its own per-body
  pairing check in parseGlobal (its own `_$gctor`/`_$gdtor`, its own messages): a group is ONE
  body and cannot be re-opened, so per-body is correct there and is NOT an inconsistency to
  "fix".

  THE EXTERNAL (out-of-line) FORM. `Class:member` defines a member of Class out of line —
  it desugars to `Class() { member }`, seeing Class's fields / consts / methods bare. Member
  kinds: const `const int C:k=7;`, alias `alias C:A=int;`, ENUM `enum int C:E ( … );` (a
  NAMED enum — its members are reached qualified, `C:E:m` / `E:m`), a METHOD `int C:m() { }`,
  an OPERATOR (a method — value-producing `bool C:op==(int a) { }` OR a no-return produce-self
  `C:op+=(int a) { }`), a CTOR/DTOR (`C:_() { }` / `C:~() { }` — a method with restrictions, so
  it takes this form like any other; class-only, so a NAMESPACE target is rejected here rather
  than relocating in as a receiver-less free `@_$ctor`), a NAMESPACE `Class:Namespace { }`
  (brace tail), and a hoisted-class RE-OPEN
  `Class:Reopen() { }` (EMPTY parens). A field-bearing head `Class:Name(fields) { }` is NOT
  this form — token-identical to inheritance (`Base:Derived(fields)`) and STAYS inheritance.

  A NAME-SLOT THAT IS NOT A NAME. Three member spellings are not identifiers — `op<sym>`, `_`,
  and `~` — and every scan that walks a `:`-chain must stop before them or misread the shape.
  isHookHead(ahead) is the ONE test for `_(` / `~(`, used by all five: parseQualifiedName (stops,
  as it already did for `:op`, leaving the head to parseFunctionDef), looksLikeQualifiedScopeDef
  and looksLikeClassDef (both of which otherwise MATCH `C:_() {}` — as a hoisted-class re-open of
  a class named `_`, and as an empty-field derived class named `_` — token-identical shapes where
  the hook must win, exactly as the class-body loop peels `_`/`~` off before looksLikeClassDef),
  looksLikeFunctionDef (an early-out: an out-of-line hook head has no return type, so the
  return-type scan would eat the qualifier and then find `:` in the name slot), and
  parseFunctionDef's qualifier loop (which mints the same `_$ctor`/`_$dtor` name the in-class form
  does). `~` is not an identifier and `_` is reserved wherever a member may be named, so neither is
  spellable as a member name and there is nothing to disambiguate against.

  THE LEADING CHAIN IS THE QUALIFIER. An out-of-line member with NO return type — `C:op+=(…)`,
  `C:_()`, and the CHAINED `A:B:op+=(…)` / `A:B:~()` — leads with the qualifier where a return type
  would sit, so parseFunctionDef reinterprets what parseDeclarator ate. That reinterpretation must
  cover the WHOLE chain: parseQualifiedName swallows all of `A:B` as the "type", so a one-token
  test (the old `pos == type_start + 1`) caught only the single-segment forms and reported
  "Expected function name" at the `:` for every chained one — which is why `A:B:op+=` was broken
  long before hooks existed. The spelling is split back into segments (splitQualifiedSpelling) and
  a token-count check (a bare chain of n segments is 2n-1 tokens) confirms the declarator consumed
  exactly the colon chain and no type suffix, so segment -> token stays exact for the carets.
  The TAIL disambiguates at grammar (looksLikeQualifiedScopeDef, checked BEFORE
  looksLikeClassDef): `{` -> namespace, `()` -> class re-open, `(fields)` -> inheritance. The
  empty-parens `A:B() {}` is genuinely AMBIGUOUS with an empty-field DERIVED class
  `A : B() {}` (token-identical); grammar always routes it as a re-open, then relocation (below)
  decides SEMANTICALLY: if A is a CLASS whose openings do not already contain B, it is
  reinterpreted in place as inheritance (B derives from A); B already in A -> re-open; A a
  namespace -> create nested. The form works in ANY scope the class is DECLARED in — file,
  namespace body, class body, function body / nested block — because relocation runs per-scope
  (below), not only over program->children.

  RELOCATION. grammar tags a qualified head with node->qualifier (the target path):
  parseFunctionDef parses `Ret A:B:m`; parseQualifiedScopeDef parses `A:B:X {` / `A:B:X() {`;
  parseEnumDecl parses a qualified enum name `enum int A:B:E ( … )`. resolve.
  relocateOutOfLineMembers, a pre-pass BEFORE registration in EVERY scope (over
  program->children, plus registerScopeNames / resolveStmtList / resolveFunctionBody for
  namespace / class / function bodies), walks the qualifier path via collectScopeOpenings —
  scope-in-scope through classes AND namespaces, searching ALL openings at each level so a
  segment introduced in a re-open is reachable — and MOVES the node into the target's children
  (a LOCAL sibling), where the ordinary machinery handles it with no special-casing. A method
  whose immediate scope is a CLASS gets the implicit `_$recv` spliced in HERE (via
  parse::makeReceiverParam); the parser does NOT add a receiver to a qualified method in a
  class body (else `_$recv` doubles — Outer's from the parser, the target's from relocation).

  SAME-SCOPE for classes; namespaces open anywhere. A CLASS re-open is same-scope: the target
  must be a local sibling opening in THIS `children`, so re-opening a class merely VISIBLE from
  an enclosing scope (refine) fails the local walk and errors per-segment. A NAMESPACE opens in
  ANY scope, so a qualified LEAF (const/alias/enum) whose first segment names an enclosing-scope
  namespace is not physically movable — registerQualifiedLeaf registers it into that namespace's
  frame IN PLACE (node left for constfold; the intermediate resolve passes skip a qualified
  leaf). A qualified MUTABLE var is not a member ("Only constants, aliases, and enums may be
  defined by qualified name"). The chained form `Class1:Ns1:Class2:Ns2:Class3:method` follows;
  an external namespace MERGES with an in-block namespace of the same name and re-opens repeatedly.

  DIAGNOSTICS. A missing path segment is careted at the SPECIFIC failing segment, named against
  its parent scope: `'Gone' is not a class or namespace in scope` (first segment) /
  `'Onest' has no class or namespace member 'Gone'` (a later one). A refine attempt (external
  member on a class merely visible from an enclosing scope) hits the same first-segment
  message. (`Ns:Member()` of a non-existent NAMESPACE member silently creates a new empty
  class, consistent with the block field-less create-or-re-open rule; but `Class:New()` of a
  non-existent member of a CLASS is instead read as INHERITANCE — the empty-field derived
  `Class : New()` — see the empty-parens disambiguation above.) OUT OF SCOPE for
  re-open proper: `global` vars, `...` incomplete classes, and the cross-scope run-time variant
  (REFINEMENTS, above). Canon test/class/reopen.sl.


VIRTUAL CLASSES (landed; spans grammar / resolve / classify / desugar / codegen)

  A class with >=1 `virtual` member is a virtual class: it carries a vtable pointer for
  runtime dispatch. Composes with single inheritance (the `_$base` slot-0 subobject) and
  re-open. Canon test/class/virtual.sl.

  LAYOUT — vptr at OFFSET 0 (C++ ABI). A ROOT virtual class gets a hidden `_$vptr` as its
  unnamed FIRST field (parse::hasVptr, field_names[0] == "_$vptr"); a DERIVED virtual class
  reuses the base's vptr through `_$base` and has NO `_$vptr` of its own — the two are
  mutually exclusive at slot 0. `_$vptr` rides the class layout exactly like `_$base`: sizeof
  grows one pointer, it is never a constructor argument (flatFieldWidth / classifyClassInit
  skip it), and field access resolves by name over the shifted slots.

  VTABLE. Each virtual class emits `@<Class>__$vtable`, a `[N x ptr]` constant: SLOT 0 is the
  COMPLETE destructor `@<Class>__$dtor` (the same complete dtor every scope/delete site calls;
  there is no separate `__$vdtor` — it collapsed into `__$dtor`), virtual methods occupy slots
  1+. Slot map (desugar
  buildVtables / vtableOf, memoized): base slots first (a stable index valid in every derived
  vtable), an override REUSES its base slot, a new virtual APPENDS, and OVERLOADED virtuals
  each take their own slot (overload resolution picks the slot at compile time, the vptr picks
  the impl at run time). A PURE slot is `ptr null`. buildVtables stamps g_entry_slot
  (entry -> slot) and fills ast::Tree::vtables; signature match is parse::userParamsEqual,
  class -> frame is parse::classNsFrame.

  DISPATCH. A `self.` / `obj.` / `ptr^` virtual call loads the vptr at offset 0, GEPs the
  method's slot, loads the fn ptr, and calls indirect: desugar lowerMethodCall sets
  call->vtable_slot = g_entry_slot[id] + 1 (the +1 skips the dtor at slot 0) when the target
  is virtual and NOT bypass_virtual; codegen emitCall emits the indirect callee. So an
  override wins at runtime even through a base pointer, and an inherited method resolves to
  the base slot. `delete` of a virtual pointer dispatches the destructor through slot 0.

  VPTR STAMP — construction AND destruction, per class, inside the complete methods. The
  complete `@<Class>__$ctor` stamps the vtable at offset 0 AFTER building the base/fields (so
  each class's own ctor calls its base's complete ctor first, then overwrites with its own
  stamp) and BEFORE the user ctor body — so a virtual call inside a base ctor dispatches to the
  class UNDER CONSTRUCTION, and the most-derived vtable ends up installed. The complete
  `@<Class>__$dtor` RE-STAMPS the vtable at entry, before the user dtor body and the reverse
  field teardown, so as teardown walks toward the root the vptr "downgrades" and a virtual
  call inside a base dtor dispatches to the class UNDER DESTRUCTION — never to a more-derived
  override whose object part is already gone (the C++ rule; must be this way or a torn-down
  override runs). A virtual class ALWAYS needs-ctor/dtor (validateVirtualClass +
  widen::setSlidNeeds), so its vptr is stamped even with no user ctor/dtor — no node synthesis.

  PURE / ABSTRACT. `virtual T m(...) = delete;` is a bodyless kFunctionDecl (is_pure), exempt
  from the orphan "declared but never defined" check, a valid dispatch target (null slot), and
  makes its class ABSTRACT. A pure method MUST be virtual (rejected at parse otherwise). The
  abstract-instantiation check lives at the ONE construction funnel classifyClassInit, gated
  by a `subobject` flag: a base subobject may be abstract (the concrete derived completes its
  pure slots) and a by-value FIELD is diagnosed at the class definition (classifyScope), while
  every genuine instantiation — a local, `new`, a temporary, an array/tuple element — is
  rejected uniformly.

  RULES (resolve validateVirtualClass, over ALL classes incl. re-opens): the base of a virtual
  class must be virtual; an explicitly declared dtor must be virtual; an override must be
  declared `virtual` and match the inherited return type (NO covariance); a virtual method may
  not shadow a non-virtual one (nor vice versa); a re-open may override/implement an existing
  (inherited or original) slot but may NOT add a NEW virtual method. Multiple inheritance is
  not supported.

  BYPASS. A qualified method call is a STATIC dispatch bypass (parse.h bypass_virtual, set by
  resolve tryResolveBaseQualifier). ALL FOUR spellings bypass — `Base:m()` / `Self:m()` (base +
  own-class qualifier) and `Base:self.m()` / `Self:self.m()` (the same with an explicit
  `self.`). selfOrBaseDepth unifies them (0 hops = own class, d = a transitive base, -1 =
  unrelated -> defer), and is_method walks classAndBaseFrames so a qualifier naming a class
  that only INHERITS the method still bypasses (to the nearest impl) and `X:m()` agrees with
  `X:self.m()`. Only an unqualified `self.m()` dispatches.

  GOTCHA. A virtual method's DEFAULT ARGUMENT binds from the STATIC receiver type (a call-site
  rewrite) while the body is chosen dynamically — a base pointer uses the base's default even
  when it dispatches to a derived override. Deliberate (the C++ rule); see todo "VIRTUAL
  DEFAULT ARGUMENTS". A class returned BY VALUE (sret), float64, and int64 all dispatch fine.


A CLASS ACROSS TRANSLATION UNITS (landed 2026-07-16; Phase 8 slice; single-`.slh` module)

  WHERE A CLASS IS DECLARED DECIDES ITS LINKAGE. Not where it is used, not which file is being
  compiled — the declaration site alone. widen::Type::Linkage rides the class TYPE, set in
  resolve's registerClassBody (the only place that sees every opening) and read by codegen as
  the choice between three emissions:

    kInternal  the class is declared in a `.sl`. It is PRIVATE to this TU — nothing outside can
               name it — so every member, synthesized or written, emits `define internal`. This
               is also a FIX, not just a policy: two unrelated `.sl` files each declaring a
               class named `Sh` used to emit the same global `@Sh__$ctor` twice and fail to
               link. Two such classes are now unrelated by construction.
    kDefine    the class is declared in a `.slh` AND this TU is its SIBLING (same base name;
               the directories may differ). The sibling emits the SYNTHESIZED members —
               complete ctor/dtor, default copy/move/swap — as real external definitions.
    kDeclare   the class is declared in a `.slh` and this TU is not the sibling. It only
               `declare`s those symbols and links to the sibling's.

  EVERY `.slh` REQUIRES A SIBLING `.sl`. Confirmed intent, even for a POD class, even if the
  `.sl` is empty: a header-only module is impossible, because SOMEONE must emit the synthesized
  members exactly once and the sibling is the only TU-independent answer to "who". (Templates
  will want the same rule, so it is a rule about headers, not about classes.)

  SYNTHESIZED IS THE SIBLING'S; DECLARED IS ANYONE'S. The two halves have different rules and
  the difference is the whole design. A SYNTHESIZED member is emitted only by the sibling —
  nobody wrote it, so no source file's location can select an owner. A DECLARED member (a
  method, an operator, a hook body) may be defined in ANY ONE `.sl` — the use case is a
  `library.slh` declaring several classes with a source file each. Zero or two definitions is a
  LINK error, not a compile error: this compiler sees one TU and cannot know what the others
  define, and that is the same deal `foo()` declared in `foo.slh` already gets.

  A SOURCE FILE CANNOT ADD AN IMPLICIT MEMBER TO A HEADER CLASS. The five implicitly-invoked
  members — ctor, dtor, copy, move, swap — are called WITHOUT the author naming them, off a
  declaration going out of scope, off a `=`, off a slot-wise transfer. So every importing TU
  emits those calls from the HEADER ALONE. One that exists only in some `.sl` would make that
  TU disagree, silently, with every other about what constructing or copying the class does,
  and its symbol would collide with the sibling's synthesized default besides. THE BAN IS ON
  ADDING, NOT ON DEFINING — that word is load-bearing: `Animal:_() { }` in the sibling is legal
  because `library.slh` declares `_();`. So the check asks whether the HEADER declared the
  member, never where the definition lives, and it applies to the sibling exactly as to any
  other source. Enforced in registerClassBody over every opening (a re-open in a `.sl` is one
  of the spellings that adds one), against parse::isImplicitMember. Canon test/import.

  A HEADER HOLDS DECLARATIONS ONLY — the companion rule, enforced in the PARSER because it is
  about author code; see readme.txt's grammar entry for why no later stage can ask it.

  THE VTABLE STAYS PRIVATE. It is stamped by the ctor, so only the ctor's implementation needs
  to know what it is — and that is the sibling's. No `@C__$vtable` crosses a TU.

  OPEN. `sizeof` (an external synthesized function for an INCOMPLETE class, an internal constant
  otherwise — deferred with cross-TU incomplete classes); cross-TU globals; overloads (they
  mangle per-TU); and a HOOK BODY IN A NON-SIBLING TU, which mis-compiles today — see todo.txt.
