# Slids vs C++

## Structure
- `()` signifies data; `{}` indicates code
- `.slh` header files are declarations only — no code, no template bodies
- Two pass compilation - no need for forward declarations
- Private fields and methods can be added to classes after the fact
- Simplified syntax
- No lambdas, no recursive functions
- `import` instead of `#include`
- No preprocessor
- Underscores in integer literals for readability - `1_000_000`

## Memory and pointers
- `^` = reference (no arithmetic), `[]` = iterator (arithmetic) — two distinct pointer types vs C++'s one
- `^` before takes the address instead of `&` - `^` after a pointer dereferences instead of `*` before or `->` after
- `delete` sets the deleted pointer to nullptr
- move operator `<-` - sets rhs to nullptr
- `size_t` is `intptr` — is signed, not unsigned.

## Type system
- immutable type inference
- type conversion is an assignment to a temporary variable `(type=expr)`
- assignment is a statement, not an expression - cannot do x = y = 0; cannot do if (x = 0)
- new `transport` keyword for new concept - completing incomplete classes

## Templates
- Templates compiled exactly once - even if in different source files.

## Operators
- Logical exclusive or `^^`
- Complete set of augmented assignment operators - `^^=`
