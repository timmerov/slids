# Slids vs C++

## Structure
- `()` signifies data; `{}` indicates code
- `.slh` header files are declarations only — no code, no template bodies
- Two pass compilation - no need for forward declarations
- Private fields and methods can be added to classes after the fact
- `transport` imports incomplete classes, exports annotated classes
- Simplified syntax
- `import` instead of `#include`
- No preprocessor
- Underscores in integer literals for readability - `1_000_000`
- `{}` required after `if` `while` `for`
- Always pass by reference, not value
- Object instantiation initializes all parameters then calls constructor
- `self` object instead of `this` pointer.

## Memory and pointers
- `^` = reference (no arithmetic), `[]` = iterator (arithmetic) — two distinct pointer types vs C++'s one
- `^` before takes the address instead of `&` - `^` after a pointer dereferences instead of `*` before or `->` after
- `delete` sets the deleted pointer to nullptr
- Move operator `<-` - sets rhs to nullptr
- Builtin swap operator '<->'
- `size_t` is `intptr` — is signed, not unsigned.

## Type system
- Immutable type inference
- Type conversion is an assignment to a temporary variable `(type=expr)`
- Assignment is a statement, not an expression - cannot do `x = y = 0;` cannot do `if (x = 0)`
- Reinterpret pointer cast syntax `<int^>` `<void^>` `<float^>`
- Pointers can only be cast to/from `intptr`, `int8`, `uint8`, `void`, a subclass, or super class

## Control Loops
- `for`, `while`, `switch` loops can be named - `inner` `outer`
- Can `break` or `continue` from nested loops
- `for` syntax is under development

## Functions
- No lambdas - nested functions instead.
- Can return tuples - `(int a, int b) foo();`

## Templates
- Templates compiled exactly once - even if in different source files.

## Operators
- Logical exclusive or `^^`
- Complete set of augmented assignment operators - `^^=`
