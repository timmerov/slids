compiler_v2 — CLASSES (companion to readme.txt)

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
    slot stays correct — no re-run. Canon test_v2/class/field.sl; [[project_inferred_field_types]].
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
    SPREADS its tuple slot-to-field; the call form keeps each arg whole. A size-1
    init tuple is inexpressible (`( x )` collapses) — punted (todo). A class — OR an
    array/tuple whose leaves are classes (widen::hasInPlaceClass, recursing array
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
    miscompiled. Detail: test_v2/class/nameless.sl.
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
    NoInitClass)` in test_v2/class/nameless.sl.
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
    with the class type), codegen does a whole-value store (+ emitNullLeaves for the
    move) and STILL runs the ctor, so a copied/moved object is constructed exactly
    once and balances its dtor. Default only (no user op=/op<- yet); canon
    test_v2/class/operator.sl.
  * CTOR/DTOR are scope HOOKS, not the constructor — fields are initialized first,
    the ctor runs after, the dtor at scope exit. `_(){}` / `~(){}` parse as
    kFunctionDef with an implicit receiver param `_$recv` (`Name^`); a bare field
    name in the body rewrites to the spec `self.field` = `_$recv^.field` (resolve
    method_fields fallback — locals shadow).
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
    test_v2/class/empty.sl.
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
    above.) Canon test_v2/class/method.sl + test_v2/function/call.sl. The `self` KEYWORD is an
    address-aliased LOCAL of the class type whose storage IS `_$recv`'s target:
    `self`, `self.field`, `self.m()`, and `^self` (= `_$recv`, its own address) all
    flow through ordinary local machinery — resolve registers it in
    resolveFunctionBody (like a param, not in body_locals); codegen binds its SymTab
    address to `_$recv^` once at the prologue (self_entry_id on the fn node). Bare
    `x_ += 1` field compound-writes resolve to the field too (the kAugAssignStmt
    rewrite). A CONSTRUCTION receiver in a method call (`Class(2).m()`) is lifted to
    a `_$cret` temp: in a decl-init / arg / return / STATEMENT it lifts to the
    enclosing scope; in a CONDITION (if/while/for/switch) it lifts into the
    condition's PPID seq (kSeqExpr), constructed at phrase entry and DESTROYED after
    the value — so a loop receiver is rebuilt+destroyed each iteration and an
    if-temp dies before the body. Codegen's kSeqExpr builds/destroys the class temp;
    desugar's lowerPhraseSlot lifts it (lift_constructions, for conditions only — a
    return / arg builds in place). A `&&`/`||` short-circuit RHS lifts into its OWN
    sub-seq (lowerPhraseSlot recurses per short-circuit operand), so a skipped branch
    runs no ctor/dtor; the unconditional LHS lifts into the condition pre. Still
    todo.txt: a construction in a store-target / move operand (rejected). Detail:
    [[project_self_and_method_calls]].
  * BARE-FIELD REWRITE reaches MORE expression contexts. Besides reads / `=` writes /
    compound writes, the `bare field -> self.field` rewrite now fires for a `##type`
    OPERAND (`##type(field)` -> the field's type) and an ADDRESS-OF operand (`^field`
    -> `^self.field`, using the `self` keyword so the addr-of walk descends to a
    resolvable base). So a bare field works as a value, a store target, a `##type`
    operand, and under `^`. Canon test_v2/class/field.sl.
  * METHOD / FUNCTION PARITY — methods get OVERLOADING + DEFAULT PARAMS + INFER-PARAM-
    TYPE-FROM-DEFAULT (the three callable features free functions already had) through
    ONE shared overload engine. pickOverload(cands, args, recv_offset) is factored out
    of classifyCall (exact 0 / widen 1 cost, lowest-total wins; tie = "Ambiguous call",
    none = "No matching overload") and used by BOTH classifyCall (recv_offset 0) and
    inferMethodCall (recv_offset 1 — `_$recv` is held out of ranking + the arity range).
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
    fields), not a signature-bearing callable. Canon test_v2/class/overload_cls.sl +
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
  (see RE-OPENING CLASSES below). Canon test_v2/class/inheritance.sl
  (Stages 1-5; positives incl. synthesized ctor/dtor, field shadowing, transitive sizeof +
  single heap derived; negatives: implicit downcast, unrelated/sibling cast, off-chain
  qualifier, cycle + direct self-inheritance, name hiding).


RE-OPENING CLASSES + THE EXTERNAL FORM (landed; spans grammar / resolve; non-virtual)

  A class may be RE-OPENED to add members after its primary definition, as a BLOCK
  (`Class() { ... }`) or via the EXTERNAL qualified form (`Class:member`). Every opening
  merges into ONE class — members are visible bare or qualified across ALL openings. The
  PRIMARY (field-bearing) definition must come first; the class's LAYOUT is the primary's
  kSlid, and a field-bearing re-open is rejected ("Duplicate definition of class 'X'; a
  re-open cannot add fields"). Re-open is a SAME-SCOPE construct — all openings live in the
  scope where the class is declared. The CROSS-SCOPE / run-time-scope variant (re-opening a
  class from a scope that isn't its own) is a SEPARATE, not-yet-landed feature — REFINEMENTS:
  a scoped zero-field derived class ($T) that USURPS the base's name in the scope and only
  LOOKS like re-opening (rides the landed inheritance + a free offset-0 cast; motivated by
  giving a generic like `sort<T>` a method the element type lacks). Canon test_v2/class/refine.sl.

  BLOCK RE-OPEN. A same-name class with an EMPTY field list re-opens the existing one.
  registerClassName points the re-open node's resolved_entry_id at the PRIMARY's entry and
  marks it is_reopen; registerScopeNames recurses the opening's members into the SHARED frame
  (the persistent ns_frame_id that namespaces already reuse), so const / alias / enum / method
  / nested class / nested namespace all land in one member set. The field-body pass and the
  class-collection loops SKIP an is_reopen node (guarded by the flag), so the primary's fields
  / lifecycle / layout are never clobbered; flattenScope lifts each opening's methods under
  `<Class>__method` for free. A re-open of a BASE is visible on a DERIVED instance (it rides
  the _$base chain); a nested class introduced by a re-open, and re-opened again, both work.

  THE EXTERNAL (out-of-line) FORM. `Class:member` defines a member of Class out of line —
  it desugars to `Class() { member }`, seeing Class's fields / consts / methods bare. Member
  kinds: const `const int C:k=7;`, alias `alias C:A=int;`, ENUM `enum int C:E ( … );` (a
  NAMED enum — its members are reached qualified, `C:E:m` / `E:m`), a METHOD `int C:m() { }`,
  a NAMESPACE `Class:Namespace { }` (brace tail), and a hoisted-class RE-OPEN
  `Class:Reopen() { }` (EMPTY parens). A field-bearing head `Class:Name(fields) { }` is NOT
  this form — token-identical to inheritance (`Base:Derived(fields)`) and STAYS inheritance.
  The TAIL disambiguates at grammar (looksLikeQualifiedScopeDef, checked BEFORE
  looksLikeClassDef so the empty-parens re-open isn't grabbed as an empty-field derived
  class): `{` -> namespace, `()` -> class re-open, `(fields)` -> inheritance. The form works
  in ANY scope the class is DECLARED in — file, namespace body, class body, function body /
  nested block — because relocation runs per-scope (below), not only over program->children.

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
  message. (Finding: `Class:Reopen()` of a non-existent hoisted class silently creates a new
  empty class, consistent with the block field-less create-or-re-open rule.) OUT OF SCOPE for
  re-open proper: `global` vars, `...` incomplete classes, and the cross-scope run-time variant
  (REFINEMENTS, above). Canon test_v2/class/reopen.sl.


VIRTUAL CLASSES (landed; spans grammar / resolve / classify / desugar / codegen)

  A class with >=1 `virtual` member is a virtual class: it carries a vtable pointer for
  runtime dispatch. Composes with single inheritance (the `_$base` slot-0 subobject) and
  re-open. Canon test_v2/class/virtual.sl.

  LAYOUT — vptr at OFFSET 0 (C++ ABI). A ROOT virtual class gets a hidden `_$vptr` as its
  unnamed FIRST field (parse::hasVptr, field_names[0] == "_$vptr"); a DERIVED virtual class
  reuses the base's vptr through `_$base` and has NO `_$vptr` of its own — the two are
  mutually exclusive at slot 0. `_$vptr` rides the class layout exactly like `_$base`: sizeof
  grows one pointer, it is never a constructor argument (flatFieldWidth / classifyClassInit
  skip it), and field access resolves by name over the shifted slots.

  VTABLE. Each virtual class emits `@<Class>__$vtable`, a `[N x ptr]` constant: SLOT 0 is the
  COMPLETE destructor `@<Class>__$vdtor`, virtual methods occupy slots 1+. Slot map (desugar
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

  VPTR STAMP — construction AND destruction, per class. emitConstructHooks stamps the vtable
  at offset 0 BEFORE the ctor body, per class as the object builds up (base first) — so a
  virtual call inside a base ctor dispatches to the class UNDER CONSTRUCTION, and the
  most-derived vtable ends up installed. emitDestructHooks RE-STAMPS the vtable before EACH
  class's dtor body, so as teardown walks toward the root the vptr "downgrades" and a virtual
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
