# TODO

## Globals

Globals are fully landed at the language level. Phases 1-6 plus reverse-construction-order, cross-TU static access via `.slh`, and cross-TU lazy access via stable namespace-path mangling on sentinel + ensure widget. The phase-3 runtime is a per-TU dtor node + ctor + dtor (internal), sentinel + ensure widget at default linkage so consumers can gate cross-TU, and a `linkonce_odr` shared head + walker. Header-side decls support inferred-type short form (`global what_ = 2;`), bare typed (`int where_;`), and forward-decl ctor/dtor (`_(); ~();`).

Remaining genuine technical gaps (not coverage artifacts):

- **Threads — `global.self` / `global.default` paired with `new.self` / `new.default`.** Explicit future-work in the spec. Per-thread (TLS) variant for the default; cross-thread default uses a lock-protected heap on the `new` side. Out of scope until a thread story exists at the language level.

- **Header/def initializer-match check.** Spec says the header's initializer (when present) must match the defining TU's value. Today the consumer silently accepts whatever the def says and ignores the header's value. Add a comparison in `parser.cpp`'s post-parse dedup before the local entry wins out.

- **Header/def *partial* mismatch — sharpen the error.** The dedup pass in `parser.cpp` already throws when a header namespace's fields are partially redefined locally, but the message is generic. A note pointing at the specific missing/extra fields would be clearer.

- **Missing def at link time.** Header declares a lazy global, no TU provides ctor/dtor or storage. Today this surfaces as a generic linker "undefined symbol" error against the mangled symbol. A slidsc-side diagnostic at the aggregator step (`runInstantiate`) would tie it back to source.

- **Three-or-more TU programs.** The cross-TU runtime should compose for N > 2 TUs (sentinels are still unique per slid; the shared head is still one list), but it's not been exercised. A two-consumer-plus-one-definer sample would pin this.

- **Aggregator (`--instantiate`) in the build pipeline.** The aggregator's collision check and globals summary work, but no `make` target invokes `--instantiate` today, so the path can regress silently. Wire it into the multi-TU sample build, then exercise the cross-TU collision negative.

- **Lazy ctor that touches a *cross-TU* lazy.** The intra-TU pattern (one ctor touches another lazy) works and pins reverse-construction order. Cross-TU equivalent — ctor in TU1 touches a lazy in TU2 — should fire the second ensure widget and end up with the right LIFO order across TUs. Plumbing is in place; not exercised.

- **Lazy diagnostics.** Forward-decl pair-rule (`_();` without `~();`), header carrying a ctor/dtor body (should the header reject those, or accept-and-prefer-def?), bodyless-ctor-only-in-def — these are policy questions plus error-path tests, not core feature gaps.

- **Slid-typed global fields with implicit construction.** Globals require foldable-constant initializers, and a slid instance isn't foldable — so `global Class instance;` and `global wrap(Class c_) { }` (no foldable `=` on `c_`) zero-init the slid storage and never call `Class:_()`. Two paths: (a) reject the shape at parse time with a clear message pointing the author at the explicit-wrapping workaround (`global wrap(Class^ c_ = nullptr) { _() { c_ = new Class(); } ~() { delete c_; } }`); or (b) extend the model so a slid-typed field with no foldable default implicitly turns the wrapping global lazy, with a compiler-generated `_()` calling the field type's ctor and a matching dtor registered on the shared list. Sibling of the deferred slid-field-default codegen bug — fix that first, then pick a path.

## Compiler

- **Expand ##name scope**: Currently `##name(expr)` only accepts a bare variable reference (`VarExpr`). Consider extending to field access (`obj.field` → `"field"` or `"obj.field"`), array index (`arr[i]` → `"arr"`), and other lvalue forms.

- **##value operator**: Implement `##value(expr)` for runtime-to-string conversion of enum values and bools. Enums require a lookup table (name → string); bools are a simple conditional. This needs runtime code emission and a table-generation pass during codegen.

- **Future stringification macros**: `##pathname` (full source file path), `##function_mangled` (linker-mangled name), and other compile-time introspection macros noted in `stringification.txt`.


- **Optimize temporary object usage**: Allow a class to declare `op reset() { ... }` that returns the object to a valid default state. When this overload exists, the compiler should reuse the same temporary slot across successive operations — avoiding the allocate/free cycle entirely. This is especially valuable for types like `String` where each construction involves a heap allocation.

- **Auto-generated transport header signatures**: When producing a `.slh` transport header, allow marking a declaration with `= auto` to have the compiler derive and emit the full signature from the implementation. For example, `hello = auto;` in the header spec would expand to `void hello(char[] greeting);` in the exported `.slh`, eliminating the need to hand-write signatures for transport types.

- **Deleted operators**: Allow marking an operator as deleted to prevent its use. `op=(SameType^) = delete;` inside a class body disables the synthesized default copy — `SameType x; SameType y = x;` becomes a compile error. Applies to any operator, not just copy.

- **Improve compiler error messages**: Error messages currently report only a line number and a token-level surprise (e.g. "expected '=', got '('""). They should also report the source file name, show the offending source line, and — where possible — give a higher-level description of what construct was being parsed (e.g. "in slid method declaration").

- **Bounds check fixed-size arrays indexed by literals**: When a fixed-size array field or local (e.g. `int rgb_[3]`) is indexed by an integer literal (e.g. `rgb_[3]`), the compiler has enough information to catch the out-of-bounds access at compile time and emit an error. Currently it silently writes past the end of the array. Anon-tuple `tuple[N]` already does this — same approach (resolve N via `constExprToInt`, range-check against the type's known size) applies to fixed-size arrays.

- **Revisit array handling**: Review how arrays are declared, passed, indexed, and iterated — including whether `int` pointer arithmetic increments correctly and whether pointer math in general is correct. Make a sample file to exercise these cases.

- **Forbid shadowing type names with variable names**: Using a class name as a variable name should be a compile error. `String String = "..."` is currently not caught and causes ambiguities and vexing parses — the parser cannot tell whether `String` in expression position refers to the type or the variable. Builtin type keywords (`int`, `float32`, etc.) are already safe since they are reserved tokens; user-defined class names are identifiers and need an explicit check.

- **Const-fold: support `sizeof`**: Phase 1 substitution constants reject `sizeof(...)` in the rhs. Allow it once class layout is settled at fold time — the value comes from `slid_info_.sizeof_override` or a layout walk. Worth pairing with any other compile-time-knowable scalars.

- **Const inside templates referencing the template parameter**: Phase 1 supports consts inside template classes/methods only when the rhs is template-independent (folded once, same value across instantiations). When the rhs references `T` (e.g. `const T zero = 0;`, `const int n = sizeof(T);`), fold per instantiation after substitution. Needs the fold pass to run during template instantiation, not just in the program-level pre-pass.

- **Template methods inside a class body**: `Container(...) { T identity<T>(T a) { ... } }` — parser errors at `Expected ';', got '<'`. Adjacent to the const-in-templates story: the const-in-nested-template-method test case from test/const.sl is blocked on this until template methods land at the language level.

- **Templates across translation units — remaining work:**
  - `@impl "other"` annotation in `.slh` to override the same-name convention for the impl file
  - Name conflict detection: if two `.sli` files list `instantiate add<Value>` but `Value` comes from different class headers, `--instantiate` should emit a compile error (same mangled name `add__Value` from two different types)
  - Emit `alwaysinline` or `inlinehint` LLVM attribute on template instantiations
  - The instantiator should internally build the `.sl` file contents and only overwrite the existing `.sl` file if the contents differ; also ensure the build system (Makefile) does not treat an unchanged file as dirty (so `make` does not unnecessarily rebuild dependents)

- **Heap-allocated anon-tuples**: Support `tup_ptr = new (int, int);` and `tup_ptr = new (int, int)(10, 20);` for heap allocation of anonymous tuples. Today `new` only accepts named types. Need parser disambiguation (the type after `new` already accepts anon-tuple syntax via `parseTypeName`, but the trailing `(args)` vs no-init form needs grammar rules) and a codegen path that allocates `sizeof(<struct>)` bytes and either zero-fills or runs ctor-style per-slot init.

- **Template type-parameter matching against tuple shapes**: Allow a template T to bind through a tuple-shaped parameter so `void print<T>( (char[], char[], T)^ p )` infers T from the third slot of the call site's tuple arg. Also adds the named-tuple-as-parameter form `void print<T>( (char[] type, char[] name, T value) )` where the inner element names become local bindings (sugar for an anonymous tuple-ref param plus a leading destructure into the named slots). Mirrors the existing named-tuple-return syntax, on the input side.

- **Revisit `tuple.count()` and runtime-indexed tuple access**: `tuple.count()` would expose element count as a compile-time constant — useful for compile-time iteration (template recursion / `static for`). Runtime-indexed tuple access (`tuple[i]` with non-constant `i`) is currently rejected because heterogeneous tuples can't be type-checked at runtime. Either feature on its own is small; together they enable generic tuple-traversal patterns. Decide whether to introduce a compile-time-unroll construct, restrict to homogeneous tuples for runtime indexing, or leave as-is.

- **Testing:**
  - Need unit tests and regression tests for pretty much everything.
  - Naming conventions: Claude used naming conventions in the parser. Test to ensure the user can use lower case classes and upper case functions.
  - Functions declared in `.slh` are public entry points in `.o` files. Functions defined in `.sl` files are private and not exported in `.o` files. `main` and the lifecycle hooks (`__$ctor`, `__$dtor`, `__$sizeof`) of importable classes are exceptions. Explicit template instantiation is also public. Test this by trying to access a private imported class method.
  - **Sharpen deferred negative diagnostics.** `make -C test negatives` reports SKIP for cases marked `//-EXPECT-ERROR-DEFERRED:`. Pointer compound-assign, reference arithmetic, pointer/reference cross-ops, tuple size/heterogeneity mismatches, and for-over-heterogeneous-tuple all landed. Remaining deferrals are pure parser gaps, not wording: anon-tuple-element fixed-array decls (for.sl), swap-operand pre-increment `(--p1)^ <-> (++p2)^` (swap.sl).

- **Returning:** Currently, a non-void function must end with a return statement - which is flawed but it kinda sorta works. We need to ensure every possible code path returns. And don't require a return if the end of block is unreachable.

- **For-loop syntax — remaining work.** All six shapes lower to `ForLongStmt` at parse time. Iter-class dispatch uses option (D) + compat-as-availability: loop-var reference shape picks when both protocols defined; one defined handles the matching loop-var shape; inferred-both-defined errors. See [[project_for_disambiguation_d_landed]] and [[project_for_loop_design]]. Spec doc lags the new (D) rule; defer doc update until directed. Still to do:
  - **One-protocol-handles-both-shapes for begin/end/next**: today the loop var binds to begin's return (the iterator). To allow `for (T x : begin_only_class)` where T is the *element*, synthesize a hidden `__iter` and bind the user's loop var via `loop_var = __iter^` (by-value) or `loop_var = __iter` (by-ref) at the body head. Op[]-only classes already work for both shapes natively.
  - **Loop-forever literal-fold check** for ranges composed entirely of integer literals (e.g. `step == 0`, `start == end` with strict cmp, sign mismatch between cmp direction and step sign).
  - **Multi-dim array iteration semantics**: `int board[8][8]; for (x : board)` — three options on the table (flat / compile error / row iteration with `int^`); user hasn't chosen.
  - **Multi-overload op[]/begin**: spec leans yes-but-deferred. Today `recordSlidMethods` records last-seen signature only.
  - **Param-typed source iteration**: when the for source is a function parameter, the parser doesn't track the type, so the dispatch falls into the "neither protocol" error with an empty type name. Fix: thread param types into `parseBlock`'s predeclare list.
  - **widensTo / LLVM-form widen rank-table reconciliation**: see [[reference_widens_to_helper]]. Two rank tables (parser-side slids strings vs codegen-side LLVM types) encode the same rules; factor or query.

- **Elide slid prvalue temps into known slots** — *partially landed*. Tuple-literal slot init now constructs the prvalue ctor call directly at the slot (codegen_expr.cpp TupleExpr arm + codegen_stmt.cpp slid-tuple VarDeclStmt arm). `for (Simple x : (Simple(1), Simple(2)))` now runs 3 ctor / 3 dtor; tuple.sl Action blocks tightened. Slid local `Simple s = Simple(1)` already elided pre-existing. Remaining sites:
  - Field init: `obj.field_ = Simple(1)` materializes a temp, dispatches op= on the field. Elision into a populated field requires dtor-then-ctor — semantically distinct from slot init; deferred.
  - Array elem init: `Action arr[3] = (Action(0), Action(1), Action(2))` blocked by ArrayDeclStmt's lack of per-slot ctor-with-args support.
  - Function arg slot: not a target — slids forbids class-type by-value parameters.

- **Codegen scope state — frames host locals.** After unifying `locals_` + `local_types_` into a single `LocalInfo { reg; type; }` map and adding the `scope_stack_` for snapshot/restore, the next step matches the parser's literal shape: move the locals map *into* each scope frame so reads walk the stack innermost-first. Enables true cross-scope shadowing (an inner-scope local hiding an outer-scope name without losing the outer entry on pop) and cleaner break/continue cleanup (iterate frames between current point and target loop, dtor each). Touches ~268 read sites that currently hit the flat map. Mechanical sweep: replace `locals_.find/at/count/[]` with helpers that walk `scope_stack_`. Defer until the immediate scope-stack discipline lands and shakes out.

- **Optimize returning objects:**
  - Currently, a function returning an object copies the object to its retval. The retval should be the object - named value return optimization (NRVO).
    - **Status**: single-slid sret returns already do NRVO (the function writes directly into the caller's `%retval`).
    - **Tuple returns do not yet NRVO** — they build a transitional `ret_tup` alloca, populate per slot, load the struct, and `ret` the value (the caller then stores into its slot — second copy). NRVO for tuple returns would require a sret-style protocol where the caller passes a destination ptr.
  - Copying objects about to be destructed should use move semantics.
    - **Status (partial)**: in the sret path, `op<-` is preferred over `op=` when the ret-value source is a fresh slid temp.

- **Destructure-target FieldAccess slots**: `(p.a_, x) = (10, 20);` is rejected by the parser today (TupleDestructureStmt allows only bare names, typed names, or empty slots). Decide whether to extend the spec to allow `obj.field` and `arr[i]` slots; if yes, parser + codegen support is mechanical.

- **`__println` chars-as-ints in concatenation**: When `__println` concatenates a `char` with `+`, the segment formats as an integer (`buf[dbca]=(100,98,99,97)` from swap.sl line 126). The intent is `(d,b,c,a)`. The format dispatch in the println intrinsic doesn't pick `%c` for char-typed segments; it falls through to `%d`. Add a `char` branch to the per-segment format dispatch in the println codegen.

- **PPID — residual gaps after phrase-based scheduling.** All phrase sites in the spec are wired (statement-in-block, call/method/ctor arg slots, tuple-literal slots, anon-tuple destructure slots, if/while/long-for cond, switch scrutinee, long-for init slots, `&&`/`||` right operand). Pre-extract walks the phrase AST at entry; post-extract drains the queue at exit. Compound-assign single-eval covers slid LHS with `op<op>` (no `op<op>=`) inline. Remaining:
  - **Complex pre-operand re-fire.** When a pre's operand has its own side effects (e.g. `++(arr[i++])`), `resolveLvalue` is called once during pre-pass and again during eval — for simple lvalues this is idempotent, but the index in the example would re-fire `i++`. Fix: cache the resolved lvalue in `pre_done_stack_.back()` so eval skips re-resolution.
  - **Anon-tuple LHS compound-assign** still falls through to the clone-desugar (Step 3 in CompoundAssignStmt arm), which re-evaluates the LHS sub-expression. Single-eval would emit element-wise op-store in place or recurse per-slot through `resolveLvalue`.

- **Virtual dispatch through chained-lvalue receivers.** The `indirect` flag in MethodCallStmt (codegen_stmt.cpp:2535) and MethodCallExpr (codegen_expr.cpp:498) triggers vtable dispatch only when the receiver is a `DerefExpr` or `self`. Receivers that resolve through `resolveLvalue` — chained AIE (`arr[i][j].virt()`), slid array (`sims[0].virt()`), AIE-through-FieldAccess (`obj.tup[i].virt()`) — fall to static dispatch even when the slot's runtime type may differ from its static type. Fix: extend `indirect` to also fire when the resolved receiver is reached through any chain that could hold a derived class (anon-tuple slot of a virtual base, slid-array of a virtual base, etc.). Pre-existing; surfaced by the chain-method-dispatch unification.

- **Method call on non-lvalue receiver.** `foo().bar()`, `(a + b).bar()`, etc. error with "Complex method call" because the receiver isn't an lvalue and `resolveLvalue` can't resolve it. Fix: mirror the rvalue-base spill in `emitExpr(ArrayIndexExpr)` (codegen_expr.cpp:66) — when the receiver is a non-lvalue producing a slid value, spill to a temp alloca first and dispatch on that. Need lifetime tracking for the temp (dtor scheduling) — same shape as the op[]→slid sret-temp concern in the chain fix.

- **Multi-dim slid arrays.** `Simple board[3][3]` is rejected with "Multi-dimensional fixed-size array of slid type '…' is not yet supported." (defensive — the 1D per-slot path would emit wrong storage for the 2D case). To enable: extend the structural multi-dim walker in ArrayDeclStmt to call `emitConstructAt` / `emitInitFieldsAtPtrs` at each leaf instead of the primitive store, with the same shape rules (outer arity = dims[n-1], inner slots recurse). Tail-short slots default-construct rather than zero-fill. No test in scope uses this; add one when implementing.

- **Unify the per-site tuple-init desugar into a single helper.** *Priority: when the next change in this area lands.* The tuple-init landing (initialization.sl spec) wired the desugar into 6+ codegen sites — VarDeclStmt slid LHS, VarDeclStmt anon-tuple LHS, AssignStmt slid LHS, AssignStmt anon-tuple LHS, ArrayDeclStmt (multi-dim flatten + single tuple-source unpack + per-slot recursion + leaf type check), ReturnStmt tuple-return path, plus the leaf type check in `initFieldFromExpr` (codegen.cpp:3563). Each site repeats the same dispatch ladder (TupleExpr → unpack as overrides; matching tuple-shape source → struct copy / field-walk; non-matching tuple-shape → per-slot copy with type checks; single value → single-arg promotion; primitive leaf → store with width-coerce + slids-type-equality short-circuit). A factored `emitDesugaredInit(dst_addr, dst_type, src_expr, is_decl)` helper would put the spec rule in one place, reduce drift between sites, and let future shapes (assignment skip-syntax, named-tuple sources, slid-RHS once it leaves out-of-scope) land at one site instead of six. Defer until the next reason to touch this area arrives — current implementation works, all in-scope tests pass.

- **Support `float` as a first-class spelling, like `int`/`intptr`/`char`.** Today the lexer canonicalizes `float` → `float32` at token time (`lexer.cpp` line 192: `if (value == "float") return Token(TokenType::kFloat32, "float32");`), so the type-string the parser/codegen see is always `"float32"` even when the source said `float`. By contrast, `int` and `int32` are independent spellings (separate tokens, separate type strings, both map to `i32` in `llvmType`) — and `intptr`/`char` each have their own slids-level identity. `float` should match: keep the source spelling as `"float"`, have downstream code (llvmType, widensTo, foldConstExpr's slid_type, etc.) recognize `"float"` and `"float32"` as the same underlying 32-bit float type. Touches: lexer rule for `float`, possibly a new `kFloat` token (or kFloat32 with value="float"), `llvmType`'s float branch, `widensTo`'s float-rank table (parser.h), `foldConstExpr` float-literal classification, and a primitive-types row in `slids_reference.md`. Side benefit: `##type(pi_)` would print `"float"` instead of `"float32"` when the author wrote the friendly spelling.

- **Allow string-literal and tuple-literal defaults for inferred fields.** Today `Foo(x_ = "hello")` and `Foo(x_ = (1, 2))` reject as "Expression is not allowed in a constant initializer" because `foldConstExpr` handles only int/float literals, var refs, type-conv, unary, and binary expressions. To support them, extend `foldConstExpr` to fold `StringLiteralExpr` (type = `char[]`, store the string value) and `TupleExpr` (type = the inferred anon-tuple type, store the element values). Inferred fields with these defaults then derive the right types (`char[]` and `(t1, t2, ...)`) naturally. Touches: `foldConstExpr`'s case set, `ConstEntry`'s storage (would need to hold string/tuple values), `emitConstValue`. Separate task from the inferred-fields feature; not blocking.

- **Slid-typed tuple-field default codegen bug (deferred).** Adding a slid-typed field (e.g. `Simple s_`) to a class's tuple emits malformed IR `store %struct.Simple 0, ptr ...` in some contexts. The minimal-shape bug-repro file in `bugs/` does *not* trigger — the trigger needs ingredients beyond a bare slid-typed field. Worked around by avoiding the field in the test that surfaced it. See [[project_slid_field_default_bug_open.md]] for the bisection plan when this surfaces again.

- **For-loop shadowing of outer-scope locals.** `int row = 0; for (row : 0..8) { ... }` declares a fresh loop-scope `row` rather than reassigning the outer one. After the loop, the outer `row` still reads as 0. Surfaced by `sample/chess1.sl`, whose `Knight found at row X, col Y` print reports the un-updated outer values. May be by-design (loop scope hygiene + standard for-init semantics) or a bug — needs a design decision. If intentional, chess1 should declare loop-local vars instead; if the loop is supposed to reassign when a same-named local is in scope, the for-init parser/codegen should look up the existing local rather than allocating a new one.

- **Unify index and deref through reference-returning ops — landed.** `op[]=` removed; `op[](idx)` returns `T^`; `op^()` arity 0 added. Reads via `c[i]` desugar to `c.op[](i)^`; writes via `c[i] = x` to `c.op[](i)^ = x`. Same shape for iterator-style classes via `op^()`. Arity 2 of op^ stays binary XOR; arity 1 reserved. See [[project_op_index_deref_landed]]. Remaining: tighten return-type validator (today no compile-error if op[] returns a value type — fails downstream cryptically); audit const propagation on the returned ref (op[] on a const String returns mutable `char^`).

## Virtual methods (design, not yet implemented)

Single-inheritance virtuals through a per-class vtable. Compatible with incomplete-class reopens (impl can have hidden virtuals invisible to consumers).

- **Layout**: one vtable per class as static rodata; one vptr at offset 0 of each polymorphic instance. Slots `0..P-1` are public virtuals (declared in `.slh`); slots `P..N-1` are hidden virtuals (declared in impl/friends); derived classes append at `N..`.
- **Hidden virtuals**: emitted with two symbols — the mangled internal name and a slot alias `<Class>__$vtable_method_<i>`. The impl also emits a sentinel `<Class>__$vtable_size = N`.
- **Consumer of an imported virtual class** reads the sentinel from the impl `.o`, emits its derived vtable as rodata wiring slot aliases by name into hidden slots; emits a `.o` dependency via existing `-M`. Class→impl mapping uses the same `foo.slh` → `foo.o` convention as templates (`@impl "other"` override).
- **ABI rules** match C++: bodies of any virtual change freely; add / remove / reorder virtuals (public or hidden) requires consumer rebuild. Slot order is locked by declaration order in source. The sentinel makes layout changes queryable.
- **Reserved cap (optional)**: producer publishes N larger than actual count, fills padding with abort-stubs; later versions populate padding without bumping N.
- **Validation**: at consumer compile time, header's public count P must match the first P slot aliases in the impl `.o`. Mismatch is a compile error against the header.
- **Out of scope**: multiple inheritance, RTTI/dynamic_cast, trait-object-style decoupled vtables, forward-declared deriving (no `.o` known).
