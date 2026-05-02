# TODO

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

- **Returning:** Currently, a non-void function must end with a return statement - which is flawed but it kinda sorta works. We need to ensure every possible code path returns. And don't require a return if the end of block is unreachable.

- **Unary operator overloads (deferred)**: Unary `op-`, `op~`, `op!` are not supported. Removed from the parser's `kOpSymbols` table. Both modes can coexist on a single class, distinguished by arity: 0-arg with primitive return (`bool op!()` — operand-self, "ask the object," e.g. `if (!obj)`); 1-arg with no return (`op!(T^ a)` — destination-self, "transform into self," e.g. `obj2 = !obj1`). `op-` additionally accepts the 2-arg form (binary subtraction). Implementation: re-add to `kOpSymbols`, extend `checkOpArity` to allow 0 and 1 explicit arg for unary symbols, and add a slid-typed-operand fork in `UnaryExpr` codegen that dispatches to the op overload before falling back to primitive icmp / neg.

- **For-loop syntax**: Two surface forms over one construct. **for-iterator** (short): `for (var : iterable) { body }` — `:` is parser sugar for `) (`. **for-mation** (long): `for (init) (cond) { step } { body }` — four enclosure groups; `(init)`/`(cond)` are data tuples, `{step}`/`{body}` are code blocks. Short desugars to long; bounds are hoisted to the init tuple so they evaluate once. Range expressions (`lo..hi`, `lo..<hi`, `lo..<=hi`) construct `Range<T>` values; the comparison suffix selects the cmp operator stored in the Range. Today `Range<T>` is compiler-blessed (parallel to `__println`); planned to become a libslid template class with pre-compiled common instantiations deleted (`Range<int> = delete;` etc.) to prevent user re-instantiation. Tuple iteration (`for (x : tuple)`) compile-time unrolls per slot. Iteration protocol shared by Range, containers, and author-defined iterables (exact shape TBD).

- **Optimize returning objects:**
  - Currently, a function returning an object copies the object to its retval. The retval should be the object - named value return optimization (NRVO).
    - **Status**: single-slid sret returns already do NRVO (the function writes directly into the caller's `%retval`).
    - **Tuple returns do not yet NRVO** — they build a transitional `ret_tup` alloca, populate per slot, load the struct, and `ret` the value (the caller then stores into its slot — second copy). NRVO for tuple returns would require a sret-style protocol where the caller passes a destination ptr.
  - Copying objects about to be destructed should use move semantics.
    - **Status (partial)**: in the sret path, `op<-` is preferred over `op=` when the ret-value source is a fresh slid temp.

## Virtual methods (design, not yet implemented)

Single-inheritance virtuals through a per-class vtable. Compatible with incomplete-class reopens (impl can have hidden virtuals invisible to consumers).

- **Layout**: one vtable per class as static rodata; one vptr at offset 0 of each polymorphic instance. Slots `0..P-1` are public virtuals (declared in `.slh`); slots `P..N-1` are hidden virtuals (declared in impl/friends); derived classes append at `N..`.
- **Hidden virtuals**: emitted with two symbols — the mangled internal name and a slot alias `<Class>__$vtable_method_<i>`. The impl also emits a sentinel `<Class>__$vtable_size = N`.
- **Consumer of an imported virtual class** reads the sentinel from the impl `.o`, emits its derived vtable as rodata wiring slot aliases by name into hidden slots; emits a `.o` dependency via existing `-M`. Class→impl mapping uses the same `foo.slh` → `foo.o` convention as templates (`@impl "other"` override).
- **ABI rules** match C++: bodies of any virtual change freely; add / remove / reorder virtuals (public or hidden) requires consumer rebuild. Slot order is locked by declaration order in source. The sentinel makes layout changes queryable.
- **Reserved cap (optional)**: producer publishes N larger than actual count, fills padding with abort-stubs; later versions populate padding without bumping N.
- **Validation**: at consumer compile time, header's public count P must match the first P slot aliases in the impl `.o`. Mismatch is a compile error against the header.
- **Out of scope**: multiple inheritance, RTTI/dynamic_cast, trait-object-style decoupled vtables, forward-declared deriving (no `.o` known).
