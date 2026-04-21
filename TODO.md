# TODO

## Compiler

- **Optimize temporary object usage**: Allow a class to declare `op reset() { ... }` that returns the object to a valid default state. When this overload exists, the compiler should reuse the same temporary slot across successive operations — avoiding the allocate/free cycle entirely. This is especially valuable for types like `String` where each construction involves a heap allocation.

- **Auto-generated transport header signatures**: When producing a `.slh` transport header, allow marking a declaration with `= auto` to have the compiler derive and emit the full signature from the implementation. For example, `hello = auto;` in the header spec would expand to `void hello(char[] greeting);` in the exported `.slh`, eliminating the need to hand-write signatures for transport types.

- **Deleted operators**: Allow marking an operator as deleted to prevent its use. `op=(SameType^) = delete;` inside a class body disables the synthesized default copy — `SameType x; SameType y = x;` becomes a compile error. Applies to any operator, not just copy.

- **Improve compiler error messages**: Error messages currently report only a line number and a token-level surprise (e.g. "expected '=', got '('""). They should also report the source file name, show the offending source line, and — where possible — give a higher-level description of what construct was being parsed (e.g. "in slid method declaration").

- **Bounds check fixed-size arrays indexed by literals**: When a fixed-size array field or local (e.g. `int rgb_[3]`) is indexed by an integer literal (e.g. `rgb_[3]`), the compiler has enough information to catch the out-of-bounds access at compile time and emit an error. Currently it silently writes past the end of the array.

- **Revisit array handling**: Review how arrays are declared, passed, indexed, and iterated — including whether `int` pointer arithmetic increments correctly and whether pointer math in general is correct. Make a sample file to exercise these cases.

- **Forbid shadowing type names with variable names**: Using a class name as a variable name should be a compile error. `String String = "..."` is currently not caught and causes ambiguities and vexing parses — the parser cannot tell whether `String` in expression position refers to the type or the variable. Builtin type keywords (`int`, `float32`, etc.) are already safe since they are reserved tokens; user-defined class names are identifiers and need an explicit check.

- **Templates across translation units — remaining work:**
  - `@impl "other"` annotation in `.slh` to override the same-name convention for the impl file
  - Name conflict detection: if two `.sli` files list `instantiate add<Value>` but `Value` comes from different class headers, `--instantiate` should emit a compile error (same mangled name `add__Value` from two different types)
  - Emit `alwaysinline` or `inlinehint` LLVM attribute on template instantiations
  - The instantiator should internally build the `.sl` file contents and only overwrite the existing `.sl` file if the contents differ; also ensure the build system (Makefile) does not treat an unchanged file as dirty (so `make` does not unnecessarily rebuild dependents)

- **Testing:**
  - Need unit tests and regression tests for pretty much everything.
  - Naming conventions: Claude used naming conventions in the parser. Test to ensure the user can use lower case classes and upper case functions.
  - Functions declared in `.slh` are public entry points in `.o` files. Functions defined in `.sl` files are private and not exported in `.o` files. `main` and `__pinit` are exceptions. Explicit template instantiation is also public. Test this by trying to access a private imported class method.
