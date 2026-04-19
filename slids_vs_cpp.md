# Slids vs C++

## Memory and pointers
- `^` = reference (no arithmetic), `[]` = iterator (arithmetic) — two distinct pointer types vs C++'s one
- `delete` nullifies the pointer after freeing — no dangling pointers
- `<-` move operator is explicit pointer theft, not a special case of assignment
- `sizeof` returns signed `intptr`, not unsigned `size_t` — avoids underflow bugs

## Type system
- `x = 0;` infers type — no `auto` keyword needed
- `(type=expr)` explicit conversion syntax — no silent implicit conversions
- `char` is unsigned (like `uint8`), `int` is always 32-bit, `intptr` is pointer-sized signed
- `transport` keyword for opaque/incomplete types — language-level pimpl

## Templates
- Template bodies in `.sl` files, not `.slh` headers — no per-TU recompilation
- Each instantiation compiled exactly once via `--instantiate` pass
- Type inference with widening rules and slid-over-primitive preference

## Structure
- `()` signifies data; `{}` indicates code — consistent throughout the language
- `.slh` files are declarations only — no code, no template bodies
- No forward declarations within a `.sl` file — two-pass compiler
- `.slh` is the public contract, `.sl` is private implementation — enforced by the file model
- No separate `class`/`struct`/`function` hierarchy — everything is a slid
- No lambdas, no recursive functions

## Operators
- `op+`, `op=`, `op<-`, `op<->` — named explicitly, no magic syntax
- Operator overloads disambiguated by type-suffix mangling, not C++ SFINAE
