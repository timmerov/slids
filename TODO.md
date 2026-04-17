# TODO

## Compiler

- **Default copy operator**: If a class does not define `op=(SameType^)`, the compiler should synthesize one that copies fields one by one. Currently, `Value v1 = v0` accidentally works for single-field structs (falls back to `op=(int32)` and reads the right bits by coincidence), but silently skips extra fields on multi-field classes.

- **Forbid shadowing type names with variable names**: Using a class name as a variable name should be a compile error. `String String = "..."` is currently not caught and causes ambiguities and vexing parses — the parser cannot tell whether `String` in expression position refers to the type or the variable. Builtin type keywords (`int`, `float32`, etc.) are already safe since they are reserved tokens; user-defined class names are identifiers and need an explicit check.
