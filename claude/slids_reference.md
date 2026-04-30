# Slids language reference

## Overview

Slids is a compiled, systems-level programming language with C++-like power, cleaner syntax, and better error messages [claude is lying -timmer]. Source files compile to `.o` object files compatible with the g++ linker via the LLVM C++ API (Itanium C++ ABI).

The language has exactly one construct: the **slid**. A slid serves as both function and class. Calling a slid executes its body and returns an instance of itself. Nested slids are methods. Everything — functions, classes, methods, constructors — is a slid.

---

## Naming conventions

> **Note:** This document uses Google C++ style naming conventions in its examples. These are not required by the language — any consistent naming convention may be used.

| Item | Convention | Example |
|---|---|---|
| Slid type names | `UpperCamelCase` | `Counter`, `Vec2`, `FileHandle` |
| Function/method names | `lowerCamelCase` verb phrase | `openFile`, `increment`, `computeLength` |
| Local variables | `snake_case` | `file_name`, `max_count` |
| Tuple member fields | `snake_case_` (trailing `_`) | `value_`, `x_`, `file_name_` |
| Constants | `kUpperCamelCase` | `kMaxSize` |
| Enum values | `kUpperCamelCase` | `kNorth`, `kSouth` |

---

## Toolchain

| Item | Value |
|---|---|
| Source extension | `.sl` |
| Header extension | `.slh` |
| Compiler | `slidsc` |
| Backend | LLVM C++ API (direct) |
| Object format | ELF / COFF / Mach-O (platform native) |
| ABI | Itanium C++ ABI (g++ compatible) |
| Linker | g++ |

**Build example:**
```
slidsc foo.sl -o foo.o
g++ foo.o bar.o -o app
```

---

## File model and visibility

- `.slh` — header file. All declarations here are **public**.
- `.sl` — source file. Everything here is **private** unless declared in the matching `.slh`.
- No `public` / `private` keywords — the header is the contract.
- Everything in a `.slh` is a forward declaration — no executable code.
- `.sl` files are compiled in two passes — the first pass collects all slid names and signatures, the second pass resolves references. This means forward declarations are never needed within a single `.sl` file, even for mutually recursive slids or classes defined later in the file.

`.slh` rules:
- A slid used as a **function** — signature only, no `{}`
- A slid used as a **class** — must have `{}`, but the body may only contain method declarations and nested class declarations, no executable code
- A slid **forward declared as a class** — name followed by `;`, no tuple, no `{}`. Tells the compiler the name is a class defined elsewhere. Allows using a pointer to the type without importing its full definition.
- POD constants and simple data initializations are allowed

A slid is recognized as a method declaration (not a constructor) when its return type differs from its name. A slid is recognized as a forward declaration when it has no `{}` body.

**widget.slh** — uses a `Counter^` without importing `counter.slh`:
```
Counter;   // forward class declaration — Counter is defined elsewhere

Widget(Counter^ pccounter_) {
    int getCount();   // return type is int, not Widget — this is a method declaration
}
```

**vec2.slh** (public interface):
```
float32 dot(Vec2^ a, Vec2^ b);   // function — no {}

Vec2(float32 x_, float32 y_) {   // class — {} required, declarations only
    void init(float32 x, float32 y);
    float32 length();
}
```

**vec2.sl** (private implementation):
```
import vec2;

void Vec2:init(float32 x, float32 y) {
    x_ = x;
    y_ = y;
}

float32 Vec2:length() {
    return sqrt(x_ * x_ + y_ * y_);
}

float32 dot(Vec2^ a, Vec2^ b) {
    return a^.x_ * b^.x_ + a^.y_ * b^.y_;
}

int32 helperSlid() {  // private — not in .slh
    return 42;
}
```

---

## Primitive types

| Type | Description |
|---|---|
| `int` | signed integer (32-bit) |
| `int8` | 8-bit signed integer |
| `int16` | 16-bit signed integer |
| `int32` | 32-bit signed integer |
| `int64` | 64-bit signed integer |
| `intptr` | signed pointer-sized integer (64-bit) |
| `uint` | unsigned integer (32-bit) |
| `uint8` | 8-bit unsigned integer |
| `uint16` | 16-bit unsigned integer |
| `uint32` | 32-bit unsigned integer |
| `uint64` | 64-bit unsigned integer |
| `char` | unsigned character-sized integer (8-bit) |
| `float32` | 32-bit float |
| `float64` | 64-bit float |
| `bool` | Boolean (`true` / `false`) |
| `void` | no value |

String literals have type `char[]`. A dedicated `string` type is not a primitive — it may be provided later as a library type.


---

## Integer promotion

When operands of a binary arithmetic or bitwise operation have different sizes or signedness, they are promoted to a common type before the operation is performed. Promotion follows two steps applied in order:

**Step 1 — match sizes.** If the operands have different bit widths, the smaller one is extended to the larger width:
- A **signed** operand is **sign-extended** — the sign bit is replicated into the new high bits, preserving the value including negative numbers.
- An **unsigned** operand is **zero-extended** — the new high bits are filled with `0`, preserving the non-negative value.

**Step 2 — resolve signedness.** After the widths match, if one operand is unsigned and the other is signed, the signed operand is converted to unsigned. The result type is unsigned.

The result of the operation has the common type produced by these two steps.

```
int8   a = -1;
uint32 b = 1;
// Step 1: a (int8) sign-extended to int32 → 0xFFFF_FFFF (-1 as int32)
// Step 2: int32 converted to uint32 → 0xFFFF_FFFF (4294967295)
uint32 c = a + b;   // 0xFFFF_FFFF + 1 = 0 (wraps)

int16  x = 1000;
int32  y = 50000;
// Step 1: x sign-extended to int32 → 1000
// Step 2: both signed — no conversion needed
int32  z = x + y;   // 51000

uint8  flags = 0xFF;
uint16 mask  = 0x00FF;
// Step 1: flags zero-extended to uint16 → 0x00FF
// Step 2: both unsigned — no conversion needed
uint16 result = flags & mask;   // 0x00FF
```

Summary table:

| Left | Right | After size match | Result type |
|---|---|---|---|
| signed small | signed large | sign-extend small | signed large |
| unsigned small | unsigned large | zero-extend small | unsigned large |
| signed small | unsigned large | sign-extend small, then signed→unsigned | unsigned large |
| unsigned small | signed large | zero-extend small, then signed→unsigned | unsigned large |
| signed N | unsigned N | (same width — skip step 1) signed→unsigned | unsigned N |

---

## Type conversion — `(type=expr)`

Converts a value to a different type. The value is converted — the bits change to represent the same logical value in the target type (or as close as possible). This is distinct from pointer casting, which reinterprets bits without changing them.

The syntax is intentional: `(type=expr)` looks like an assignment because it *is* an assignment — to an anonymous temporary variable of the target type. A named declaration (`int x = expr`) is a statement; an unnamed one (`(int=expr)`) is an expression whose value is the temporary.

The parentheses are required by the parser. This was a deliberate design choice: a type keyword in expression position is otherwise meaningless, and requiring parens keeps the grammar unambiguous and makes the conversion visually distinct from a surrounding assignment. `int x = (int8=a)` clearly shows two separate `=` operations at a glance.

```
int8    b = (int8=some_int32);     // narrowing — truncates to low 8 bits
int64   x = (int64=some_int32);   // widening
float32 f = (float32=some_int32); // integer → float
int32   i = (int32=some_float32); // float → integer, truncates toward zero
uint32  u = (uint32=some_int32);  // change signedness — same bit pattern
```

Integer promotion (widening of the smaller operand in binary expressions) happens automatically — explicit type conversions are only needed to narrow or to convert between floats and integers.

Type conversions can be chained. Inside an outer `(type=...)`, the inner operand may be another conversion written without its own parentheses:

```
float32 x = (float32=int=3.14);   // 3.14 → int (3) → float32 (3.0)
```

Type conversion also works for class types. `(ClassName=expr)` creates a temporary of `ClassName` and calls its `op=` with `expr`:

```
Value v = (Value=42);    // create a Value from an int32 via op=(int32)
```

---

## Pointer casting — `<Type^> pointer-expr`

Casting reinterprets a pointer to one type as a pointer to a different type. The address is unchanged; only the type information changes. References `^` and iterators `[]` are pointers. There are rules:

**Assignment to `nullptr`:** Any pointer may be set to `nullptr` without an explicit cast.
```
Type^ ptr = nullptr;
```
**Implicit casts:** Reinterpretations that strip type information from the pointer are implicit. Explicit cast syntax is optional. Target pointer types of implicit casts are limited to: `void^`, the integer type `intptr`, and base clases in the same hierarchy.
```
void^ void_ptr = any_ptr;     // strips all type information.
intptr int_x   = any_ptr;     // convert to integer.
Base^ base_ptr = derived_ptr; // every derived pointer is a base pointer.
```
**Explicit casts to `int8`:** Reinterpretations of pointers to a generic buffer type - `int8` and `uint8`, require an explicit cast.
```
int8^ int8_ptr   = <int8^> any_ptr;
uint8^ uint8_ptr = <uint8^> any_ptr;
```
**Explicit casts:** Adding type information to a compatible pointer type requires an explicit cast. Compatible pointer types are: `void^`, `int8^`, `uint8`, and the integer type `intptr`. Pointers related by class hierarchy are compatible.
```
Type^ any_ptr = <Type^> void_ptr;
Type^ any_ptr = <Type^> int8_ptr;        // from generic buffer type.
Type^ any_ptr = <Type^> uint8_ptr;       // from generic buffer type.
Type^ any_ptr = <Type^> int_x;           // from integer.
Derived^ derived_ptr = <Derived^> base^; // not every base pointer is a derived pointer.
```
**Prohibited casts:** Directly reinterpreting a pointer as a pointer to an unrelated type is prohibited. Reinterpret indirectly. The first cast removes the old type information. The second cast adds the new type information.
```
This^ this_ptr = <This^> that_ptr;         // compile error: This and That are unrelated types.
This^ this_ptr = <This^> <void^> that_ptr; // correct method.
```
**References and iterators:** Reinterpretation of an iterator `[]` to a reference `^` of the same type is implicit. No explicit cast needed. Reinterpretation of a reference `^` to an iterator `[]` of a compatible type requires an explicit cast.
```
Type^ ref = same_type_iter;
Type[] iter = <Type[]> compatible_type_ref;
```
---

## Variables

Variables may be declared with an explicit type or with an inferred type.

**Explicit declaration:**
```
int    x = 10;
float64 y = 3.14;
bool   flag = true;
int    z = x + 1;
char[] buf = new char[64];
```

**Inferred declaration** — omit the type, let the compiler infer it from the initializer:
```
x    = 10;           // int
y    = 3.14;         // float64
buf  = new char[64]; // char[]
len  = strlen(buf);  // return type of strlen — intptr
```

The two forms are interchangeable. Explicit declarations are preferred when the type needs to be precise or when reading the code benefits from seeing the type spelled out.

---

## Type inference

When a variable name appears on the left of `=` and has not yet been declared in the current scope, the compiler infers its type from the initializer expression. The type is fixed at the point of declaration and cannot change.

```
x = 42;          // declares x as int
x = 100;         // assigns — x is already declared, still int
```

**Inference rules by expression:**

| Initializer expression | Inferred type |
|---|---|
| Decimal integer literal, fits in 32 bits | `int` |
| Decimal integer literal, larger | `int64` |
| Hex or binary literal, fits in 32 bits | `uint` |
| Hex or binary literal, larger | `uint64` |
| Float literal | `float64` |
| Character literal `'x'` | `char` |
| String literal `"..."` | `char[]` |
| `nullptr` | `intptr` |
| `new T[n]` | `T[]` |
| `^x` (address-of) | `T^` where `x` is type `T` |
| `ptr^` (dereference) | element type of `ptr` |
| `ptr++^` / `ptr--^` | element type of `ptr` |
| Variable `y` | declared type of `y` |
| Function call `f(...)` | return type of `f` |
| Method call `obj.m(...)` | return type of `m` |
| Binary expression `a op b` | inferred type of `a` |
| Type conversion `(int32=e)` | `int32` (the conversion target type) |
| `sizeof(...)` | `intptr` |

**Digit separators** — underscores are allowed anywhere inside a numeric literal and are ignored. They are purely visual:
```
big  = 3_000_000;       // int
mask = 0xFF_00_FF;      // uint
```

**Decimal vs hex/binary** — decimal literals infer signed types; hex and binary literals infer unsigned types:
```
a = 255;     // int  — decimal always signed
b = 0xFF;    // uint — hex always unsigned
```

**Assignment vs declaration** — the same `name = expr` syntax is disambiguation by scope:
- If `name` is not yet declared in the current scope, it is a declaration.
- If `name` is already declared (local variable, parameter, or class field), it is an assignment.

```
int main() {
    x = 0;         // declaration: x is int
    x = 42;        // assignment: x already declared
}
```

Field assignments inside methods are never mistaken for new declarations:
```
String(...) {
    void reserve(intptr cap) {
        storage_ = new char[cap];   // assignment to field — not a new local
    }
}
```

**Explicit declarations are always valid** and take precedence over inference when you want control over the exact type:
```
uint8 flags = 0;    // explicit: uint8, not int
intptr n = 0;       // explicit: intptr, not int
```

---

## Global variables

> **TODO:** Needs review.

> **Note:** Construction and destruction order needs to be well-defined and under programmer control — i.e. within the scope of `main()`.

---

## Numeric literals

Leading `+` is legal. Underscores are allowed anywhere in a number and are ignored — used purely as visual separators.

```
int x = 1_000_000;        // one million
int y = +42;              // explicit positive
int z = -1_000;           // negative one thousand
float32 f = 3.141_592;    // underscores in floats
int hex = 0xFF_FF;        // underscores in hex
int bin = 0b1010_0101;    // underscores in binary
```

Rules:
- Underscores may appear between digits — not at the start, end, or adjacent to `0x`/`0b` prefix
- Leading `+` is valid for any numeric literal
- Leading `-` is valid for any numeric literal

**Type of integer literals** — when used in an inferred declaration (`x = 42;`), decimal literals infer `int` if the value fits in 32 bits, or `int64` otherwise. Hex and binary literals infer `uint` if the value fits in 32 bits, or `uint64` otherwise. When assigned to a variable with an explicit type, the literal is quietly extended to match.

---

## Truth, falsity, and numeric promotion

Like C/C++, Slids has no distinct boolean type for conditionals — any integer or pointer expression can be used as a condition:

- **True** — any non-zero integer value, or any non-null pointer
- **False** — the integer value `0`, or a null pointer (`nullptr`)

`true` and `false` are integer literals `1` and `0` respectively.

The result of any comparison operator (`==`, `!=`, `<`, `>`, `<=`, `>=`) is an integer `1` (true) or `0` (false). This result can be used directly in arithmetic or assigned to any integer variable — it is quietly promoted to match the required type.

**Promotion in mixed-type expressions** — when the two operands of a binary operator have different integer sizes, the smaller operand is promoted to the size of the larger before the operation is performed. The result has the larger type. Signed and unsigned operands of the same size produce an unsigned result.

```
int8  a = 100;
int32 b = 1_000;
int32 c = a + b;   // a promoted to int32 before addition

uint8  flags = 0xFF;
uint32 mask  = 0x0000_00FF;
uint32 result = flags & mask;  // flags promoted to uint32

if (c)          { }   // true — c is non-zero
if (flags)      { }   // true — flags is non-zero
if (c == b)     { }   // comparison yields 0 or 1 (int8), promoted as needed
```

---

## Enclosure rules

- `()` — data: slid data, tuples, arguments, literals, conditions, ranges
- `{}` — code: slid bodies, if/for/while blocks

---

## Tuples

A tuple is a comma-separated list enclosed in `()`. Slids has three kinds of tuples, distinguished by what their elements look like.

### 1. Named tuple — a slid's data block

A slid's `()` data block. Each element has an explicit type and a name — these are the slid's **fields**, always reached as `obj.field_`. Fields use a trailing `_` by convention.

```
(int value_)                     // one required
(int a_, int b_)                 // two required
(int x_=0, int y_=0)             // two defaulted
(int r_, int g_=0, int b_=0)     // one required, two defaulted
```

Rules:
- Every field has an explicit type **and** a name.
- Every field has an optional initial value.
- Fields are always accessed by name.
- Required fields (no default) must precede defaulted fields.
- Required fields must be supplied by the caller; defaulted fields may be overridden.
- Every field is always initialized — to the caller-supplied value, the tuple-supplied default, or zero.

### 2. Unnamed tuple — a data list

A general-purpose ordered list. Each element has a type but no name; elements are reached **by index `[]`** or by **destructuring**.

```
tuple = (1, 2, 3);            // anonymous tuple type inferred as (int,int,int)
a = tuple[0];                 // read element 0 (type int)
tuple[2] = 10;                // write element 2

mixed = (42, "hello", 3.14);  // elements may have different types
s = mixed[1];                 // read as char[]
```

Rules:
- The anonymous tuple type is inferred from the initializer's elements.
- An index used with `[]` must be a compile-time constant integer (literal or constant-foldable). Non-constant index is a compile error.
- `tuple[N]` is read/write; out-of-range N is a compile error.
- Slid-typed elements behave like embedded fields: their destructors run (in reverse declaration order) when the tuple variable goes out of scope.
- **Anon-tuples have a minimum size of 2.** `(x)` is a parenthesized expression, not a 1-tuple; a 1-tuple would equal a scalar, so it's excluded to keep the kinds disjoint. Size 0 is meaningless.
- **Chained indexing** walks nested anon-tuples: `nested[1][0]` reads two levels. Slid or scalar slots terminate the chain.
- **Tuples pass by reference**, like classes: `(t1, t2, ...)^` on params; pass-by-value is a compile error. Inside the callee, `p^[N]` reads, `p^[N] = val` writes, `p^[N] <- val` moves.
- **Tuples can be slid fields**: `Holder((int, int) pair_) {}`. Use `obj.pair_[N]` for chained read/write; inside the slid's methods, implicit-self `pair_[N]` works too. Default values allowed: `Holder((int, int) pair_ = (0, 0)) {}`.

### 3. Destructure target — a list of variable slots

A list used on the **left** of an assignment or declaration to unpack a tuple-valued expression. Each slot is one of:
- **A bare name already in scope** — the existing variable is reassigned (no redeclaration); its type must match the corresponding RHS element exactly.
- **A bare name not yet in scope** — the variable is declared with its type inferred from the matching RHS element.
- **`type name`** — an explicit variable declaration.
- **Empty** — the matching RHS element is skipped (no variable is introduced).

The RHS may be a tuple literal, a tuple variable, or a tuple-returning function call; in every form, slid-typed targets dispatch the same per-slot copy/move logic as ordinary slid assignment.

```
(int x, int y) = (1, 2);        // explicit types
(a, b) = (42, "weird");         // inferred: a is int, b is char[]
(c, ) = (10, 20);               // skip element 1
(, d) = (10, 20);               // skip element 0
(int a, b) = (1, 2);            // mixed explicit / inferred
```

Rules:
- The number of slots (commas + 1, counting empty slots) must equal the RHS tuple's element count — size mismatches are a compile error.
- Each non-empty slot's target type (existing variable's type, or explicit `type`, or inferred type) must be compatible with the corresponding RHS element — integer widths may coerce; other mismatches are a compile error.
- Empty slots contribute no local variable.

### Tuples desugar per element

Operations on tuples desugar element-wise, recursing into nested structure. For each slot:
- A **scalar** slot emits the scalar instruction.
- A **slid** slot dispatches the user's `op` method on that slid type if defined; otherwise falls back to a default per-field walk.
- A **nested anon-tuple** slot applies the rule recursively.

```
(x, y, z) = (1, 2, 3) + (4, 5, 6);    // → x=1+4; y=2+5; z=3+6;
```

Move (`<-`) and copy (`=`) follow the same rule, dispatching `op<-` / `op=` per slot.

**Element-wise applies *within* a kind, not across kinds.** Anon-tuple + anon-tuple is element-wise. Class + class can dispatch element-wise *if* the user defines an `op+` (the compiler does not auto-fall-back to the field-walk for slid+slid). Class + tuple, tuple + class, range + tuple, etc., all require explicit user ops.

**Scalar broadcast** is the natural extension of element-wise: an anon-tuple op-with a scalar broadcasts the scalar to every slot. Operand order is preserved for non-commutative ops.

```
(1, 2, 3) + 10           // → (11, 12, 13)
2 * (1, 2, 3)            // → (2, 4, 6)
100 - (1, 2, 3)          // → (99, 98, 97)
((1,2),(3,4)) + 10       // → ((11,12),(13,14))   recurses through nested slots
```

Slot/scalar types must match exactly (no widening; this may be relaxed in the future). For slid slots, broadcast requires a user `Elem::op<op>(Elem^, scalar_type)` to be defined.

### Accessibility for tuple-literal initialization and reassignment

A tuple-literal RHS can only set fields or elements that are accessible at the call site. In a `.slh` consumer, only the slid's **public** fields can be set via a tuple literal. In the `.sl` implementer, all fields are accessible.

### Aliases (named tuples only)

> **TODO:** Not yet implemented.

A named tuple field may be aliased to another field or array element within the same tuple. Aliases are read/write and write through to the original.

**Array element aliases:**
```
Vec3(
    int v_[3],
    alias x_ = v_[0],
    alias y_ = v_[1],
    alias z_ = v_[2]
)
```

```
Vec3 a(1, 2, 3);
a.x_ = 10;        // same as a.v_[0] = 10
int n = a.y_;     // same as a.v_[1]
```

**Named field aliases** — the same data accessible under two naming conventions:
```
Color(
    int r_,
    int g_,
    int b_,
    alias y_ = r_,
    alias u_ = g_,
    alias v_ = b_
)
```

```
Color c(255, 128, 0);
c.r_ = 200;    // same as c.y_ = 200
int n = c.u_;  // same as c.g_
```

Alias rules:
- Aliases may only point to fields or array elements within the same tuple
- Aliases are not fields — they do not take up additional storage
- Aliases are always read/write — assigning to an alias writes through to the original

### Limitations

- **`new` of an anon-tuple** — `tup_ptr = new (int, int);` is not a supported syntax. Heap allocation is currently only for named types (`new SlidType(args)`, `new T[n]`). Anon-tuples live as locals or on the slid-field/tuple-slot they're embedded in. May be added.
- **Range expressions** (`a..b`) and tuple/range interaction (`2 * (1..3)`) — not designed.
- **Type widening across slots** — slot/element types must match exactly; no implicit promotion (e.g. `(int, int) + 1.5` is a compile error). May relax later.
- **Method dispatch on a slid slot reached through a tuple-returning call** — `make_tuple()[0]` reads the slot, but `make_tuple()[0].method()` is not yet wired.
- **`tuple.count()`** — not implemented. Element count is known at compile time but isn't exposed through a method.
- **Runtime-indexed tuple element access** — `tuple[i]` with a non-constant `i` is not supported because heterogeneous slots can't be type-checked at runtime. Tuples must be unrolled at compile time (no construct for that yet).

---

## Slids

A slid is the single universal construct of the language. It has:
- An optional return type (primitive, named type, or tuple)
- A name
- A parameter list `()` — inputs, or data members if the slid acts as a type
- A body `{}` — code and/or nested slid definitions

**Slid as a plain function:**
```
int add(int a, int b) {
    return a + b;
}
```

**Passing class objects and tuples to functions:**

Class objects and anon-tuples must be passed by reference using `^`. Passing by value is a compile error. Both follow the same convention.

```
void print(String^ s);            // class ref
void take_pair((int, int)^ p);    // tuple ref
void print(String s);             // compile error: cannot pass class object by value
void take_pair((int, int) p);     // compile error: cannot pass tuple by value
```

At the call site, use `^v` (explicit address-of) or just `v` (auto-promotes); a literal of the matching type is materialized to a temp and passed:

```
String s = "hello";
print(^s);                  // explicit
print(s);                   // auto-promote
print((String="world"));     // fresh temp materialized + passed

tp = (10, 20);
take_pair(^tp);             // explicit
take_pair(tp);              // auto-promote
take_pair((100, 200));      // tuple-literal materialized + passed
```

Both forms are equivalent. `^s` is preferred when you want to make the pass-by-reference explicit.

> **TODO:** `^` is intended to pass by **immutable** reference — the callee observes but cannot alter the referred-to object. A keyword (likely `mutable`, exact syntax TBD) will let the caller grant write access for cases where mutation is intended. Today's implementation does not enforce immutability; code that relies on mutating a `^` parameter will need to be updated once enforcement lands.
>
> `self` inside a method body is **mutable by default** — methods may read and write the object's fields freely. A way to mark a method as non-mutating (so `self` is treated as immutable inside that method body) is TBD.

**Returning a tuple:**
```
(bool success, int handle) openFile(string^ file_name) {
    // ...
}
```

The element names are descriptive sugar for the reader — they carry no semantic weight. The return value's type is the unnamed tuple `(bool, int)`.

The returned tuple can be assigned to a variable or destructured directly:
```
// assign to a tuple variable — access elements by index
result = openFile("data.txt");
if (!result[0]) { /* error */ }

// destructure directly — bare names are inferred, explicit types also work
(ok, h) = openFile("data.txt");
if (!ok) { /* error */ }
```

**Slid as a type — body is the constructor (no `_` or `~`):**

> **Note:** `Counter(int value_ = 0)` is shorthand for `Counter Counter(int value_ = 0)` — Counter is a function that returns an object of type Counter. The return type is omitted when it matches the slid name.

```
Counter(int value_ = 0) {
    value_ = value_ * 2;   // runs on construction

    void increment() {
        value_ += 1;
    }

    int getValue() {
        return value_;
    }
}

Counter c(5);   // value_ is now 10
```

**Slid as a type — with explicit constructor `_` and destructor `~`:**
```
Counter(int value_ = 0) {
    _() {
        value_ = 0;
    }

    ~() {
        // cleanup
    }

    void increment() {
        value_ += 1;
    }

    int getValue() {
        return value_;
    }
}
```

Constructor/destructor rules:
- `_` and `~` must always be defined together — having only one is a compiler error
- When `_` and `~` are present, the body may contain definitions only — no loose executable code

**Field initialization:**

Every field is always initialized when an instance is created, even without a constructor:
1. Caller-supplied value (from the constructor call)
2. Otherwise: the declared default value
3. Otherwise: zero (`0`, `nullptr`, or `false` as appropriate for the type)

`_()` is for additional logic beyond field initialization — heap allocation, registering callbacks, etc. If all you need is sensible defaults, no `_()` is required.

**Instantiation:**
```
Counter c;        // value_ = 0 (default)
Counter c(3);     // value_ = 3 (caller-supplied)
Vec2 v(1.0, 2.0); // x_ = 1.0, y_ = 2.0
Point p;          // all fields zero-initialized — no constructor needed
```

**Adding a single method** — use `:` scope resolution:
```
void Counter:decrement() {
    value_ -= 1;
}
```

**Adding multiple methods** — reopen the slid by name with a body only (no tuple):
```
Counter {
    void set(int x) {
        value_ = x;
    }

    void reset() {
        value_ = 0;
    }
}
```

**Incomplete slid — private fields in `.sl`**

A slid may hide additional fields inside its implementation file. The `...` ellipsis marks the split between the public prefix (visible to consumers of the `.slh`) and the private suffix (visible only inside the `.sl`).

**Header (`.slh`)** — tuple **ends** with `...` to signal that hidden fields follow elsewhere:
```
// counter.slh
Counter(int value_ = 0, ...) {
    int get();
    void increment(int delta);
}
```

**Implementation (`.sl`)** — tuple **starts** with `...`, standing in for the full public prefix, then adds the private fields:
```
// counter.sl
Counter(..., int step_ = 1) {
    int get() { return value_; }
    void increment(int delta) { value_ += delta * step_; }
}
```

Rules:
- The public prefix in the header and the `...` in the implementation must agree on order, types, and defaults.
- Consumers that import only the `.slh` see only the public fields. They cannot name, initialize, or access private fields.
- Struct size (including the private suffix) is coordinated by the compiler and linker. Consumers do not need to recompile when the private part of the implementation changes, as long as the public prefix stays the same.
- Tuple-literal initialization follows the standard accessibility rule: a `.slh` consumer can only set public fields via a tuple literal; the `.sl` implementer can set all fields.

---

## Namespaces

A namespace is a slid with no data block — a name that groups functions. Cannot be instantiated.

```
Space {
    void greet() {
        __println("howdy");
    }
}

Space:greet();          // call a namespace function
```

A leading `::` reaches the global (unnamed) namespace:

```
::greet();              // calls the top-level greet()
```

Reopening adds more functions to an existing namespace:

```
Space {
    void wave() { ... }
}
```

---

## Inheritance

Single inheritance only. Multiple inheritance and virtual methods are not yet implemented; methods are non-virtual and statically dispatched.

**Definition** — base name first, then `:`, then derived name with its own data tuple:

```
Animal(char[] name_, int legs_ = 0) {
    void speak() { ... }
}

Animal : Cat(int toys_ = 0) {
    void speak() {                    // shadows Animal:speak
        Animal:speak();               // call base method on the same self
    }
}
```

**Caller arguments** — instantiation supplies initialization values base fields first, the derived fields. A required field in the derived class forces any optional fields in the base class to be required.

```
Animal a("Snek");                     // legs_ is default
Cat c("Princess Donut");              // legs_, toys_ are default
Dog d("Spot", 4, 2);                  // sticks_ is required → legs_ is now required here
```

**Reopen and external methods** — bare and block syntax are both valid.

```
Dog { void perform() { ... } }        // block reopen
void Dog:perform2() { ... }           // external method def
```

**Pointer view**
- In-cast (derived → base) is implicit: `Animal^ a = ^cat;` needs no syntax.
- Out-cast (base → derived) is explicit, single-step: `Cat^ c = <Cat^> animal_ptr;`. No `void^` intermediate.

**Field-name collision** is a compile error.

> **TODO:** Needs review — it should be possible to add private virtual methods in the implementation file (`.sl`) that are not exposed in the `.slh`. This is similar to the desire to add private fields not exposed in the `.slh`. Both raise ABI and layout questions that need careful thought.
> A virtual class is one that has at least one virtual method. For virtual classes:
>- If `_` and `~` are explicitly defined, `~` must be declared `virtual`
>- If `~` is not explicitly defined, the compiler generates a default `virtual ~` that does nothing
>- Dtors must be virtual all the way in and out.
>- `virtual` keyword is required for all virtual methods.

---

## Operator overloading

Operator overloads are defined inside the class body using the `op` keyword followed immediately by the operator symbol. No return type is written — the return value is always `self` (the object being modified).

```
Value(int value_ = 0) {
    _() {}
    ~() {}

    op=(Value^ rhs) {
        value_ = rhs^.value_;
    }

    op=(int x) {
        value_ = x;
    }

    op+(Value^ a, Value^ b) {
        value_ = a^.value_ + b^.value_;
    }

    op+=(Value^ rhs) {
        value_ += rhs^.value_;
    }

    op+=(int x) {
        value_ += x;
    }
}
```

Supported operators: `=`, `<-`, `<->`, `+`, `-`, `*`, `/`, `+=`, `-=`, `*=`, `/=`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `[]`, `[]=`.

**Default copy operator.** If a class does not define `op=(SameType^)`, the compiler synthesizes one that copies each field by value. This means `Foo b = a;` always does the right thing for plain data types. Define your own `op=(Foo^)` to override — for example, when a field is a heap pointer that requires a deep copy.

**Default move operator.** If a class does not define `op<-(SameType^)`, the compiler synthesizes one that moves each field. Value-typed fields (ints, floats, embedded structs with no dtor) are copied; pointer and iterator fields are transferred — their value is copied to the destination and the source is set to `nullptr`. This means `Foo b <- a;` works for plain data types without any user code. Define your own `op<-(Foo^)` to override — for example, when a heap resource needs extra bookkeeping, or when the source must be left in a non-default state.

**Default swap operator.** If a class does not define `op<->(SameType^)`, the compiler synthesizes one that swaps each field. Value-typed fields and pointer/iterator fields are exchanged in place — pointers are *not* nullified, since both objects retain valid data after a swap. `op<->` may only take `SameType^`; other signatures are a compile error.

**Assignment is a statement, not an expression.** Chained assignment (`x = y = 0;`) and assignment inside a condition (`if (x = 0)`) are not allowed.

**Forward declarations** inside the class body omit the body:
```
String(int size_ = 0, int capacity_ = 0, char[] storage_ = nullptr) {
    op=(char[] s);
    op=(String^ s);
    op<-(String^ s);
}
```

---

## Control flow

### Block names

`for`, `while`, and `switch` blocks may have a name. The default name is the keyword (`for`, `while`, `switch`). An explicit name overrides the default and is written after the closing brace as `:label;`. Innermost match wins for resolution. Plain `{}`, `if`, and `else` blocks have no name.

| Block    | naked `break`              | `break name` / `break N`                       | naked `continue`         |
|----------|----------------------------|------------------------------------------------|--------------------------|
| `for`    | exit loop                  | exit named loop or Nth enclosing loop          | next iteration           |
| `while`  | exit loop                  | exit named loop or Nth enclosing loop          | re-evaluate condition    |
| `switch` | exit switch (stop fall)    | exit named switch (numbered form is loop-only) | not valid                |

Numbered `break N` and `continue N` count outward across `for` and `while` only — `switch` frames are skipped. To exit a `switch` other than the innermost, use its name. `break 1;` differs from naked `break;` when inside a switch: naked exits the switch, `break 1;` exits the enclosing loop.

Naked `continue;` inside a `switch` is a compile error even when a loop encloses the switch. Use `continue N;` or `continue name;` to advance the intended loop.

```
for int i in (0..10) {
    for int j in (0..10) {
        if (i == 5) break;            // exit inner for
        if (j == 2) continue;         // next iteration of inner for
        if (i == 7) break outer;      // exit outer for by name
    }
} :outer;
```

```
for x in (0..10) {
    switch (x) {
    default:
        break for;                    // exit the for from inside the switch
    }
}
```

### If / else

Curly brackets are always required after `if` and `else`. The only exception is `else if` — `else` may be followed directly by `if` without braces.

```
if (x > 0) {
    println("positive");
} else if (x < 0) {
    println("negative");
} else {
    println("zero");
}
```

Not legal:
```
if (true) x = 42;           // error — {} required
if (true) { } else x = 42;  // error — {} required after else
```

### While

Curly brackets are always required.

```
while (condition) {
    // ...
} :outer;   // name can be targeted by break/continue from a nested block
```

An empty condition `while ()` is equivalent to `while (true)` — loops forever until a `break` or `return`:
```
while () {
    if (done) break;
}
```

Not legal:
```
while (condition) doSomething();  // error — {} required
```

**Bottom-condition while** — executes the body first, then tests the condition. Repeats if true.

```
while {
    // do stuff
} :outer (condition);
```

### For — range-based

Curly brackets are always required.

Numeric range:
```
for int i in (0..10) {
    println(i);
} :outer;
```

Collection iteration:
```
Vector primes = (2, 3, 5);

for int i in primes {
    println(i);
}
```

Enum iteration — iterates over all values of an enum in declaration order:
```
enum Direction (
    kNorth,
    kSouth,
    kEast,
    kWest
)

for Direction d in Direction {
    println(d);
}
```

Combined with `switch` to dispatch on each value:
```
enum Piece (
    kEmpty,
    kKing,
    kQueen,
    kRook,
    kBishop,
    kKnight,
    kPawn
)

for Piece piece in Piece {
    switch (piece) {
    case kKing:   __print("King");   break;
    case kQueen:  __print("Queen");  break;
    case kRook:   __print("Rook");   break;
    default:      __print("Other");  break;
    }
}
```

Not legal:
```
for int i in (0..10) print(i);  // error — {} required
```

### Switch

Cases fall through by default. `break` stops fallthrough. `default` handles any unmatched value.

```
switch (x) {
case 1:
    println("one");
    break;
case 2:
    println("two");
    break;
case 3:
case 4:
    println("three or four");
    break;
default:
    println("other");
}
```

---

## Memory

**Allocation:**

```
int^ p = new int(42);    // allocate single int, initialized to 42
p^ = 100;
delete p;                // free memory — p is set to nullptr automatically

int[] arr = new int[42]; // allocate array of 42 ints — uninitialized (int has no constructor)
arr[0] = 1;
delete arr;              // free memory — arr is set to nullptr automatically
```

- `new Type(value)` — allocates a single object, initialized to `value`
- `new Type[n]` — allocates an array of `n` objects, each initialized by `Type`'s constructor. For types with no constructor or a no-op constructor (such as `int`), elements are uninitialized.
- `delete p` — calls the destructor for the object (or all objects if `p` is an array), frees the memory, and sets `p` to `nullptr`
- No dangling pointers after `delete` — the pointer is always nulled

### Placement new

`new(addr) Type(args)` constructs a `Type` at an existing address. No memory is allocated; the caller owns the storage. The destructor is **not** registered automatically — call it explicitly when done, then release the storage.

`addr` must be a raw-byte pointer.

```
Simple(int x_ = 0, int y_ = 0, int z_ = 0) {
    _()  { /* ... */ }
    ~()  { /* ... */ }
}

intptr size = sizeof(Simple);
int8[] where = new int8[size];               // raw storage
Simple^ obj  = new(where) Simple(1, 2, 3);   // construct in place
obj^.print();
obj^.~();                                    // explicit destructor
delete where;                                // free the buffer
```

**References (`^`) and iterators (`[]`):**

Both are pointer-like types. The difference is semantic:

| | `Type^ name` | `Type[] name` |
|---|---|---|
| Points to an object | yes | yes |
| Dereference (`name^`) | yes | yes |
| Reassign to different object | yes | yes |
| Increment / decrement | no | yes |
| Pointer arithmetic | no | yes |
| Semantics | reference to a single object | iterator over a sequence |

Unlike C++, references may be freely reassigned to point to a different object.

```
int x = 42;
int^ ptr = ^x;        // ptr points to x
ptr^ = 7;             // x is now 7

int^^ ptrptr = ^ptr;  // ptrptr points to ptr
ptrptr^^ = 3;         // x is now 3
print(x);             // prints 3

// member access through pointer
Vec2 v;
Vec2^ vp = ^v;
vp^.x_ = 10;          // instead of C++ vp->x_

// iterator
int arr[4] = (1, 2, 3, 4);
int[] iter = arr;
iter++;                // advance to next element
iter--;                // back one
int diff = iter - arr; // distance between two iterators
```

---

## Move semantics

The move operator `<-` transfers ownership of a resource from one variable to another. The source is left in a valid, empty state (its pointer is set to `nullptr`). This avoids copying and makes ownership transfer explicit in the code. For non-pointer built-in scalars (`int`, `float`, `bool`, …), `<-` is a copy.

### Pointer and iterator move

`p1 <- p2` — move a pointer or iterator:

1. Assign `p2`'s value to `p1`
2. Set `p2` to `nullptr`

`<-` does **not** free `p1`'s current allocation. If `p1` already owns memory, free it explicitly with `delete` first.

```
char[] p1 = new char[100];
char[] p2 <- p1;     // p2 = p1; p1 = nullptr — p2 now owns the allocation

// declaration move — p2 is a new variable, so no existing allocation to worry about
char[] p3 <- p2;     // p3 = p2; p2 = nullptr

delete p3;           // free — p3 is set to nullptr automatically
```

Transferring into an existing pointer that owns memory:
```
char[] buf = new char[64];
char[] tmp = new char[32];
delete buf;    // free buf's allocation first
buf <- tmp;    // buf = tmp; tmp = nullptr — buf now owns the 32-byte block
```

### Class move — `op<-`

For class types, `<-` calls a user-defined `op<-` method. The method is responsible for freeing the destination's existing resources, taking ownership of the source's resources, and leaving the source in a valid empty state.

**Declaring `op<-` in the class:**
```
String(int size_ = 0, int capacity_ = 0, char[] storage_ = nullptr) {
    op<-(String^ s);
    // ...
}
```

**Implementing `op<-`:**
```
String {
    op<-(String^ s) {
        delete storage_;            // free existing storage (delete, not <-)
        size_     = s^.size_;
        capacity_ = s^.capacity_;
        storage_ <- s^.storage_;    // steal pointer; s^.storage_ set to nullptr automatically
        s^.size_     = 0;           // leave rhs in a valid empty state
        s^.capacity_ = 0;
    }
}
```

**Using `op<-`:**
```
String s1 = "Hello, World!";

String s2 <- s1;     // s2 owns the string data; s1 is now empty

// or as an assignment into an existing variable
String s3;
s3 <- s1;            // calls s3.op<-(^s1)
```

Move rules:
- For pointer/iterator types, `<-` is built into the language — no method needed
- For class types, `<-` calls `op<-` if one is defined; otherwise it calls the compiler-synthesized default move (see Operator overloading)
- After a move, the source is left in a valid state — it can be reassigned or destroyed safely
- `delete` frees and nullifies; `<-` transfers ownership without freeing — use `delete` first if the destination already owns memory

### Swap — `<->`

`a <-> b` exchanges the values at two lvalue locations without a named temporary.

**Primitive pointer/iterator swap:**
```
char[] lo = storage_;
char[] hi = storage_ + size_ - 1;
while (lo < hi) {
    lo++^ <-> hi--^;   // swap chars at lo and hi, advance both
}
```

`lo++^` reads the address at `lo` and advances `lo` forward; `hi--^` reads the address at `hi` and advances `hi` backward. The two values are exchanged in place.

**Class swap — `op<->`:**

For class types, `<->` calls a user-defined `op<->` method:
```
String {
    op<->(String^ s) {
        // swap size_, capacity_, storage_ with s^.*
    }
}
```

Swap rules:
- For pointer/iterator element swap (`ptr++^ <-> ptr--^`), `<->` is built into the language
- For class types, `<->` calls `op<->` if one is defined; otherwise it calls the compiler-synthesized default swap (see Operator overloading)
- Both sides must be the same element type

---

## Enums

**Plain:**
```
enum Direction (
    kNorth,
    kSouth,
    kEast,
    kWest
)
```

**Typed with explicit values:**
```
enum Status : int32 (
    kOk = 0,
    kNotFound = 404,
    kError = 500
)
```

---

## Nested slids

Slids can be defined inside any `{}` block — including methods, nested functions, and nested classes. They capture variables from all enclosing scopes by reference and can nest to any depth.

```
int32 main() {
    int count = 0;

    void increment() {
        count += 1;
    }

    void addN(int n) {
        void doAdd() {
            count += n;
        }
        doAdd();
    }

    increment();
    increment();
    addN(5);
    // count is now 7

    return 0;
}
```

- Nested slids are local — not callable from outside their enclosing scope
- Capture is by reference — mutations affect the original variable

### Hoisted nested classes

A class declared inside another class is hoisted by the compiler to a top-level type with a colon-qualified name: `Outer:Inner`. The hoisted class is independent — no implicit access to the outer class's fields. Nested templates work the same way.

```
Outer(int o_ = 0) {
    Inner(int i_ = 0) {
        void show(int z) { /* ... */ }
    }
}

Outer:Inner ii(1);
ii.show(2);
```

**Non-local return** — a nested slid can cause any enclosing slid to return using `name:return`. The stack unwinds from the nested slid up to the named enclosing slid, calling destructors for all objects going out of scope along the way. The named slid then immediately returns.

> **TODO:** Not yet implemented.

```
int foo() {
    bar();

    void bar() {
        foo:return 42;   // foo immediately returns 42
                          // destructors run for all objects going out of scope
    }
}
```

Targeting any level of nesting by name:

```
int outer() {
    middle();

    void middle() {
        inner();

        void inner() {
            outer:return 99;  // unwinds through inner and middle, outer returns 99
            middle:return;    // NOTE: never reached — outer:return 99 stops execution
                               // of inner(), middle(), and outer() (after running destructors)
        }
    }
}
```

- `name:return` — non-local return from the named enclosing slid
- `name:return value` — non-local return with a value
- Destructors are called for all objects going out of scope during unwinding
- Only enclosing slids may be targeted — not siblings or outer non-enclosing slids

---

## Templates

Functions and classes can be parameterized over types using `<T>` after the name.

```
// function template
T add<T>(T a, T b) { return a + b; }

// class template
Vec<T>(T x_, T y_) {
    T length() { return sqrt(x_*x_ + y_*y_); }
}

x = add(1, 2);                  // T=int inferred from args
y = add<float64>(1.5, 2.5);     // explicit type arg
Vec<float64> v(1.0, 2.0);
```

Multiple type parameters: `Pair<K, V>(K key_, V value_)`.

**One instantiation per project build.** Declare a template signature in a `.slh` and put the body in the matching `.sl`. Each concrete instance (e.g. `Vec<float64>`) is emitted exactly once for the whole project, regardless of how many translation units use it.

**Explicit instantiation.** `instantiate Foo<int>;` forces a particular instance to be emitted.

---

## Error handling

No exceptions. Use tuple returns for success/failure:

```
(bool success, int32 value) divide(int32 a, int32 b) {
    if (b == 0) { return (false, 0); }
    return (true, a / b);
}

int32 main() {
    (bool ok, int32 result) = divide(10, 0);
    if (!ok) {
        println("division by zero");
        return 1;
    }
    return 0;
}
```

---

## Imports

> **TODO:** Needs review.

```
import std.io;
import vec2;           // imports vec2.slh from same directory
```

---

## C interop

> **TODO:** Needs review.

Use `@foreign` to declare C-linked symbols (no name mangling):

```
@foreign("printf")
int32 printf(uint8^ fmt, ...);

@foreign("malloc")
uint8^ malloc(int64 size);

int32 main() {
    printf("Value: %d\n", 42);
    return 0;
}
```

---

## sizeof

`sizeof(x)` returns the size in bytes of a type or expression as an `intptr`.

```
sizeof(char)       // 1
sizeof(int)        // 4
sizeof(int64)      // 8
sizeof(intptr)     // 8  (on 64-bit platforms)
sizeof(float32)    // 4
sizeof(float64)    // 8
sizeof(void^)      // 8  (any pointer or iterator)
sizeof(char[])     // 8
```

**Variable:**
```
int32 x;
sizeof(x)          // 4 — uses the declared type of x
```

**Stack array:**
```
char buf[256];
sizeof(buf)        // 256 — total bytes (elements × element size)
```

**Slid (struct) type:**
```
Vec3(float32 x_, float32 y_, float32 z_) {}

sizeof(Vec3)       // 12
Vec3 v;
sizeof(v)          // 12
```

**String literal:**
```
sizeof("hello")    // 5 — byte length, not including the null terminator
```

**Address-of expression:**
```
int32 x;
sizeof(^x)         // 8 — size of a pointer, not of the pointed-to type
```

`sizeof` is evaluated at compile time for all primitive and pointer types. For slid types it uses the LLVM `getelementptr null, 1` / `ptrtoint` pattern, which the optimizer folds to a constant.

---

## Stringification

Compile-time operators that produce `char[]` string literals from code structure.

### `##name(x)` — variable name

Returns the name of `x` as a string literal.

```
int some_var = 42;
char[] n = ##name(some_var);   // "some_var"
```

### `##type(x)` — variable type

Returns the declared type of `x` as a string literal.

```
int some_var = 42;
float64 f = 3.14;
char[] t1 = ##type(some_var);  // "int"
char[] t2 = ##type(f);         // "float64"
```

### `##line` — source line

Returns the current source line number as a string literal.

```
char[] ln = ##line;            // e.g. "42"
```

### `##file` — source filename

Returns the base filename of the source file being compiled.

```
char[] f = ##file;             // e.g. "main.sl"
```

### `##func` — enclosing function name

Returns the name of the function or method containing the expression.

```
char[] fn = ##func;            // e.g. "main"
```

### `##date` and `##time` — compilation timestamp

Returns the date and time at which the compiler ran, in the same format as C's `__DATE__` and `__TIME__`.

```
char[] d = ##date;             // e.g. "Apr 25 2026"
char[] tm = ##time;            // e.g. "15:30:55"
```

### `#x` — introspection tuple

Desugars to a 3-tuple `(##type(x), ##name(x), ^x)`:
- `##type(x)` — the declared type of `x` as `char[]`
- `##name(x)` — the variable name as `char[]`
- `^x` — the address of `x`, typed `T^`

The third element is a pointer rather than a value so a single tuple shape carries primitives (`int`, `float64`, …) and class objects uniformly. The `dump` template dereferences it to read the value.

```
void dump<T>( (char[], char[], T^)^ tuple );

int x = 42;
dump(#x);              // prints: int x=42
```

The operand of `#` must be a simple variable name. `##name` and `##type` have the same restriction.

---

## Comments

```
// single line comment

/* multi-line
   comment */
```

---

## Complete example

```
import std.io;

Counter(int value_ = 0) {
    void increment() {
        value_ += 1;
    }

    int getValue() {
        return value_;
    }
}

void Counter:decrement() {
    value_ -= 1;
}

Counter {
    void reset() {
        value_ = 0;
    }
}

int32 main() {
    Counter c;

    for int i in (0..5) {
        c.increment();
    }

    c.decrement();

    print("Final value: ");
    print(c.getValue());
    print("\n");

    return 0;
}
```
