# Slids language reference

## Overview

Slids is a compiled, systems-level programming language with C++-like power, cleaner syntax, and better error messages. Source files compile to `.o` object files compatible with the g++ linker via the LLVM C++ API (Itanium C++ ABI).

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
    Vec2 operator+(Vec2^ other);
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

Vec2 Vec2:operator+(Vec2^ other) {
    return Vec2(x_ + other^.x_, y_ + other^.y_);
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
| `int` | platform-width signed integer |
| `int8` | 8-bit signed integer |
| `int16` | 16-bit signed integer |
| `int32` | 32-bit signed integer |
| `int64` | 64-bit signed integer |
| `uint` | platform-width unsigned integer |
| `uint8` | 8-bit unsigned integer |
| `char` | character (alias for `uint8`) |
| `uint16` | 16-bit unsigned integer |
| `uint32` | 32-bit unsigned integer |
| `uint64` | 64-bit unsigned integer |
| `float32` | 32-bit float |
| `float64` | 64-bit float |
| `bool` | Boolean (`true` / `false`) |
| `string` | built-in string type |
| `void` | no value |


---

## Variables

> **TODO:** Needs review — strong type inference will be added after the rest of the syntax is locked in.

```
int32 x = 10;
float64 y = 3.14;
bool flag = true;
string name = "Slids";
int z = x + 1;
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

---

## Enclosure rules

- `()` — data: tuples, arguments, literals, conditions, ranges
- `{}` — code: slid bodies, if/for/while blocks

---

## Tuples

A tuple is the `()` data block of a slid. Fields are separated by `,`. Member fields use trailing `_` by convention.

```
(int)                               // anonymous field, type only
(int value_)                        // named, required — caller must supply
(int a_, int b_)                    // two required fields
(int x_=0, int y_=0)               // two fields with defaults
(int r_, int g_=0, int b_=0)       // one required, two with defaults
```

Rules:
- Every field must have an explicit type
- Fields are always separated by `,`
- Required fields (no default) must come before defaulted fields
- Fields without defaults must be supplied by the caller
- Fields with defaults may be overridden by the caller

**Aliases** — a tuple field can be aliased to another field or array element within the same tuple. Aliases are read/write and write through to the original.

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

**Returning a tuple:**
```
(bool success, int handle) openFile(string^ file_name) {
    // ...
}
```

The returned tuple can be assigned to a named tuple variable or destructured directly:
```
// assign to tuple variable
(bool success, int handle) result = openFile("data.txt");
if (!result.success) { /* error */ }

// destructure directly
(bool ok, int h) = openFile("data.txt");
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

**Instantiation:**
```
Counter c;        // default values
Counter c(3);     // value_ = 3
Vec2 v(1.0, 2.0); // x_ = 1.0, y_ = 2.0
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

> **TODO:** It should be possible to add new fields to a slid's tuple in the implementation file (`.sl`), not just in the header (`.slh`). This would allow private data members that are not part of the public interface. This is deferred — it raises ABI and layout questions that need careful thought.

---

## Inheritance

Methods are non-virtual by default — no vtable overhead unless `virtual` is explicitly used. `virtual` must be declared on the base class method and on every override in derived classes. Omitting `virtual` in a derived class when the base declares it virtual is a compiler error.

A virtual class is one that has at least one virtual method. For virtual classes:

- If `_` and `~` are explicitly defined, `~` must be declared `virtual`
- If `~` is not explicitly defined, the compiler generates a default `virtual ~` that does nothing

```
Animal(string name_) {
    _() {
        name_ = "ubu";
    }

    virtual ~() {
        // cleanup
    }

    virtual void speak() {
        println(name_ + " makes a sound.");
    }
}

Animal : Dog() {
    // compiler generates: virtual ~() {}

    virtual void speak() {
        println(name_ + " barks.");
    }
}

Dog dog("spot");
Animal^ animal = ^dog;
animal^.speak();   // calls Dog:speak
```

> **TODO:** Needs review — it should be possible to add private virtual methods in the implementation file (`.sl`) that are not exposed in the `.slh`. This is similar to the desire to add private fields not exposed in the `.slh`. Both raise ABI and layout questions that need careful thought.

---

## Operator overloading

```
Vec2(float32 x_, float32 y_) {
    Vec2 operator+(Vec2^ other) {
        return Vec2(x_ + other^.x_, y_ + other^.y_);
    }

    bool operator==(Vec2^ other) {
        return x_ == other^.x_ && y_ == other^.y_;
    }
}
```

---

## Control flow

### Block names

Every code block `{}` may have an optional name, written after the closing brace with `:`. `if`, `else`, `for`, `while`, and `switch` blocks have default names `:if`, `:else`, `:for`, `:while`, `:switch`. Default names follow standard scoping — an inner block's default name hides the outer block's default name of the same type.

`break` and `continue` rules:

| Block | naked `break` | `break name` / `break N` | `continue` |
|---|---|---|---|
| `for` | exit loop | exit named or Nth enclosing block | re-evaluate condition, next iteration |
| `while` | exit loop | exit named or Nth enclosing block | re-evaluate condition |
| `switch` | exit block / stop fallthrough | exit named or Nth enclosing block | not valid |
| `if`, `else`, plain `{}` | compiler error if unnamed | exit named block | not valid |

`break` and `continue` accept either a name or a positive integer. For numbered `break`, the integer counts outward across `for`, `while`, and `switch` blocks. For numbered `continue`, the integer counts outward across `for` and `while` blocks only — `switch` blocks are not counted. `1` is the innermost (same as naked `break`/`continue`).

```
while (true) {
    while (true) {
        break 2;    // exits the outer while loop
    }
}
```

Plain block example — `continue` is not valid, `break` requires a name:
```
{
    // do stuff
    break myBlock;   // exit the block
} :myBlock
```

`break` targeting an outer block by name:
```
for int i in (0..10) {
    for int j in (0..10) {
        if (i == 5) {
            break;             // exit inner for loop
        }
        if (j == 2) {
            continue;          // continue inner for loop
        }
        if (i == 7) {
            break outer;       // exit outer for loop by name
        }
    }
} :outer
```

`break` exiting an outer `for` from inside a `switch`:
```
for x in (0..10) {
    switch (x) {
    default:
        break outer;
    }
} :outer
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
} :outer   // name can be targeted by break/continue from a nested block
```

Not legal:
```
while (condition) doSomething();  // error — {} required
```

**Bottom-condition while** — executes the body first, then tests the condition. Repeats if true.

```
while {
    // do stuff
} :outer (condition)
```

### For — range-based

Curly brackets are always required.

Numeric range:
```
for int i in (0..10) {
    println(i);
} :outer
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

Not legal:
```
for int i in (0..10) print(i);  // error — {} required
```

### For — C-style

> **TODO:** This syntax needs review — not yet finalized.

Curly brackets are always required.

```
for (int i = 0; i < 10; i++) {
    print(i);
} :outer
```

Not legal:
```
for (int i = 0; i < 10; i++) print(i);  // error — {} required
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

Slids can be defined inside any `{}` block. They capture variables from all enclosing scopes by reference and can nest to any depth.

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

**Non-local return** — a nested slid can cause any enclosing slid to return using `name:return`. The stack unwinds from the nested slid up to the named enclosing slid, calling destructors for all objects going out of scope along the way. The named slid then immediately returns.

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

---

## Next steps

- [ ] Formal grammar (BNF / EBNF)
- [ ] Lexer implementation in C++
- [ ] Parser and AST
- [ ] LLVM IR codegen
- [ ] Standard library design
