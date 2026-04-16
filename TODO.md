# TODO

## Compiler

- **Default copy operator**: If a class does not define `op=(SameType^)`, the compiler should synthesize one that copies fields one by one. Currently, `Value v1 = v0` accidentally works for single-field structs (falls back to `op=(int32)` and reads the right bits by coincidence), but silently skips extra fields on multi-field classes.
