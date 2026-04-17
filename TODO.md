# TODO

## Compiler

- **Optimize temporary object usage**: When a class defines `op+=` (but not `op+`), an expression like `Value v = Value + 10 + 20` currently creates an intermediate temp that is then copied into `v`. Two optimization tiers:
  - **Phase 1** (straightforward): lower `Value + 10 + 20` directly to `Value temp; temp += 10; temp += 20; Value v = temp;` — chain all `+=` calls onto a single temp rather than creating a new temp per binary operation.
  - **Phase 2** (identity optimization): when the declaration target `v` is the first use of the result, either initialize `v` in place (`Value v; v += 10; v += 20;`) or "rename" the temp to `v` at alloca time, eliminating the final copy entirely. The goal is zero extra allocations and zero copy calls for this pattern.

- **Auto-generated transport header signatures**: When producing a `.slh` transport header, allow marking a declaration with `= auto` to have the compiler derive and emit the full signature from the implementation. For example, `hello = auto;` in the header spec would expand to `void hello(char[] greeting);` in the exported `.slh`, eliminating the need to hand-write signatures for transport types.

- **Deleted operators**: Allow marking an operator as deleted to prevent its use. `op=(SameType^) = delete;` inside a class body disables the synthesized default copy — `SameType x; SameType y = x;` becomes a compile error. Applies to any operator, not just copy.

- **Forbid shadowing type names with variable names**: Using a class name as a variable name should be a compile error. `String String = "..."` is currently not caught and causes ambiguities and vexing parses — the parser cannot tell whether `String` in expression position refers to the type or the variable. Builtin type keywords (`int`, `float32`, etc.) are already safe since they are reserved tokens; user-defined class names are identifiers and need an explicit check.
