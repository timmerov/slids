# TODO

## Compiler

- **Optimize temporary object usage**: When a class defines `op+=` (but not `op+`), an expression like `Value v = Value + 10 + 20` currently creates an intermediate temp that is then copied into `v`. Two optimization tiers:
  - **Phase 1** (straightforward): lower `Value + 10 + 20` directly to `Value temp; temp += 10; temp += 20; Value v = temp;` — chain all `+=` calls onto a single temp rather than creating a new temp per binary operation.
  - **Phase 2** (identity optimization): when the declaration target `v` is the first use of the result, either initialize `v` in place (`Value v; v += 10; v += 20;`) or "rename" the temp to `v` at alloca time, eliminating the final copy entirely. The goal is zero extra allocations and zero copy calls for this pattern.

  - **Phase 3** (temp reuse via `op reset`): instead of destructing one temporary and constructing the next, allow a class to declare `op reset() { ... }` that returns the object to a valid default state. When this overload exists, the compiler should reuse the same temporary slot across successive operations — avoiding the allocate/free cycle entirely. This is especially valuable for types like `String` where each construction involves a heap allocation.

- **Auto-generated transport header signatures**: When producing a `.slh` transport header, allow marking a declaration with `= auto` to have the compiler derive and emit the full signature from the implementation. For example, `hello = auto;` in the header spec would expand to `void hello(char[] greeting);` in the exported `.slh`, eliminating the need to hand-write signatures for transport types.

- **Deleted operators**: Allow marking an operator as deleted to prevent its use. `op=(SameType^) = delete;` inside a class body disables the synthesized default copy — `SameType x; SameType y = x;` becomes a compile error. Applies to any operator, not just copy.

- **Revisit array handling**: Review how arrays are declared, passed, indexed, and iterated — including whether `int` pointer arithmetic increments correctly and whether pointer math in general is correct. Make a sample file to exercise these cases.

- **Forbid shadowing type names with variable names**: Using a class name as a variable name should be a compile error. `String String = "..."` is currently not caught and causes ambiguities and vexing parses — the parser cannot tell whether `String` in expression position refers to the type or the variable. Builtin type keywords (`int`, `float32`, etc.) are already safe since they are reserved tokens; user-defined class names are identifiers and need an explicit check.

- **Templates across translation units**: Allow template functions defined in one `.sl` file to be instantiated by consumers in other `.sl` files, with each concrete instantiation compiled exactly once. Design:

  **Files:**
  - `template.slh` — forward-declares the template: `T add<T>(T a, T b);`
  - `template.sl` — provides the body (same base name as `.slh`, found by convention; or override with `@impl "other"` annotation in the `.slh`)
  - `consumer.sl` — imports `template.slh`, calls `add<int>`, `add<String>`, etc.

  **Compile phase** (per TU): When the compiler encounters a template call where the type arg is an imported type (not defined in the current `.sl`):
  - Emit `declare @add__String` in the `.ll` (external reference, no body)
  - Queue the import and instantiation: `import string;` + `add<String>;`
  - At end of compilation, flush the queue to `build/consumer.inst`

  When the type arg is a local type (defined in the current `.sl`):
  - The only TU that knows the type is this one — must instantiate here
  - Ingest `template.sl` on the spot (parse it, including its imports)
  - Emit `define @add__Value` in the current `.ll`

  **`.inst` file format** (three sections, one item per line):
  ```
  class string
  class matrix
  template template_decl
  template math_decl
  instantiate add<String>
  instantiate add<int>
  instantiate dot<Matrix>
  ```
  - `class` lines: slid types used as template args — imported first so types are known
  - `template` lines: headers declaring the template functions — imported after classes
  - `instantiate` lines: the explicit instantiation calls
  - Builtins (`int`, `float`, etc.) need no `class` line
  - `--instantiate` unions all three sections across `.inst` files (deduplicating each)
  - **Name conflict**: if two `.inst` files both list `instantiate add<Value>` but source `Value` from different class headers (e.g. `class value1` vs `class value2`), `--instantiate` emits a compile error — same mangled name `add__Value` would result from two different types. Fix by renaming one of the `Value` types.
  - Then emits:
    ```
    import string;
    import matrix;
    import template_decl;
    import math_decl;
    add<String>;
    add<int>;
    dot<Matrix>;
    ```

  **Instantiation pass** (`slidsc --instantiate build/`):
  - Read all `*.inst` files from the build directory
  - Union all `import` lines (deduplicated), collect all instantiation lines (deduplicated)
  - Emit a synthetic `__instantiations.sl`, compile it to `__instantiations.o`
  - When compiling the synthetic file, `add<String>;` outside a code block is an explicit instantiation: ingest `template.sl`, emit `define @add__String`

  **Link phase**: `g++ *.o __instantiations.o -o program` — all symbols resolve normally.

  **Makefile integration**:
  ```makefile
  __instantiations.o: $(wildcard build/*.inst)
      slidsc --instantiate build/ -o __instantiations.o
  ```

  **Inlining**: template instantiations should be emitted with `alwaysinline` or `inlinehint` LLVM attribute — they're type-specific and typically small. LTO handles cross-`.o` inlining; the attribute covers the non-LTO case.

  **Result**: each concrete instantiation (`add__int`, `add__String`) compiled exactly once, regardless of how many TUs use it. No linker deduplication needed. Local-type instantiations live in their own TU where the type is visible.
