# Slids language reference

Slids is a compiled, systems-level language with simplified, consistent syntax and minimal keywords. Source files (`.sl`) compile to LLVM IR and link with `g++`.

A **slid** is a name followed by clauses: comma-separated data in `( )`, code in `{ }`, types in `< >`. Functions, classes, templates, flow control and enums are all slids.

```
int add(int a, int b) { return a + b; }     // function
Point(int x_, int y_) { }                   // class
Pair<K, V>(K key_, V value_) { }            // template
while (n < 3) { ++n; }                      // flow control
enum Color ( kRed, kGreen );                // enum
```

The full programs behind these examples are in `examples/`. The detailed rules are in the top comment of each file in `test/`.

---

## Hello, world

```
import string;

int32 main() {
    println(String + "Hello, World!");
    return 0;
}
```

```
slidsc hello.sl -o hello.ll -I lib/hdr
llc --filetype=obj --relocation-model=pic hello.ll -o hello.o
g++ hello.o build/lib/libslids.o -o hello
```

`String + a + b + ...` builds a string from anything printable. `/* block comments */ nest`.

---

## Types and variables

`int8 int16 int32 int64`, `uint8 ... uint64`, `int uint` (32-bit), `intptr`, `float32 float64 float` (32-bit), `char`, `bool`, `void`.

```
x = 42;                     // type inferred: int
big = 5_000_000_000;        // int64
mask = 0xFF;                // uint
f = 3.14;                   // float
s = "text";                 // const char[5]

const int kSize = 4;        // compile-time constant
const y = x;                // run-time const
```

A local must be written before it is read. An unused local is a compile error.

---

## Conversions

Integers widen silently. Integers mix with floats only in `+ - * / %`. Everything else needs `(type=expr)`:

```
i = (int=3.14);             // 3
b = (bool=ptr);             // false when null
v = (Vec=5);                // class: calls Vec's op=(int)
n int = 2.5 * x;            // shorthand for n = (int=2.5 * x)
```

---

## Expressions

C operators, plus `^^` (logical xor). Zero and `nullptr` are false.

Assignment is a statement: `x = y = 0;` is an error.

`++` and `--` run before or after their whole phrase. Each argument and each tuple slot is its own phrase.

```
a = p++^;                   // read, then advance p
f(n++, ++n);                // with n = 1: f(1, 3)
```

---

## Pointers

`T^` is a reference to one object. `T[]` is an iterator into a sequence. A prefix `^` takes an address; a postfix `^` dereferences.

```
int^ ref = ^x;
ref^ = 7;
int[] it = ^arr[1];
++it;
obj^.field_ = 1;

void^ raw = ref;                // dropping type info: implicit
int^ back = <int^> raw;         // adding it: explicit cast
Base^ bp = ^derived;            // implicit
Derived^ dp = <Derived^> bp;    // explicit

int[] heap = new int[n];
delete heap;                    // frees and sets heap to nullptr
```

---

## Tuples and arrays

```
pair = (42, 'x');               // (int, char)
a = pair[0];                    // constant index
(ok, q) = divide(10, 3);        // destructure
(, r) = divide(9, 3);           // skip a slot
sum = (1, 2) + (10, 20);        // per slot: (11, 22)

int grid[2][3] = ((1, 2, 3), (4, 5, 6));

(bool, int) divide(int a, int b) {
    if (b == 0) {
        return (false, 0);
    }
    return (true, a / b);
}
```

---

## Control flow

Braces are always required.

```
if (x > 0) {
} else if (x < 0) {
} else {
}

while (n < 3) { ++n; }
while { --n; } (n > 0);         // test at the bottom

for (i : 0..10) { }             // 0 to 9
for (i : 10..>0 - 2) { }        // 10 8 6 4 2
for (x : arr) { }               // also tuples, strings, enums, classes
for (int j = 0, k = 10) (j < k) { ++j; --k; } {
    // init, condition, update, body
}

for (i : 0..5) {
    for (j : 0..5) {
        if (j == i) {
            break outer;        // or: break 2;
        }
    }
} :outer;

switch (n) {
    0: { zero(); }
    1: 2: { small(); } continue;    // continue: fall through
    default: { other(); }
}
```

---

## Functions

```
int add(int a, int b = 1, c = 2) {      // defaults; c is inferred
    return a + b + c;
}

int norm(Point^ p) {                    // read-only
    return p^.x_ + p^.y_;
}
norm(pt);                               // same as norm(^pt)

void fill(mutable int[] dst, int n);    // mutable: may write
```

Primitives are passed by value. Classes, tuples and arrays are passed by address. Functions overload and nest; a nested function sees the enclosing locals.

---

## Classes

```
Counter(int value_ = 0, int step_ = 1) {
    _() { }                             // ctor
    ~() { }                             // dtor

    void bump() {
        value_ += step_;
    }
    int const get() {                   // const: does not modify self
        return value_;
    }
}

void Counter:reset() { value_ = 0; }    // external method
Counter() { const int kMax = 100; }     // re-open

Counter a;                              // defaults
Counter b(10, 5);                       // fill fields, then ctor
Counter c = (20, 2);                    // defaults, ctor, then assign
x = Counter(7).get();                   // temporary: dies at the ;
```

`_` and `~` take no parameters and come as a pair. Fields with no value are zero.

---

## Operators

```
Vec(int x_ = 0, int y_ = 0) {
    op=(int v) { x_ = v; y_ = v; }
    op+(Vec^ a, Vec^ b) { x_ = a^.x_ + b^.x_; y_ = a^.y_ + b^.y_; }
    bool const op==(Vec^ v) { return x_ == v^.x_ && y_ == v^.y_; }
    int^ op[](int i) { if (i == 0) { return ^x_; } return ^y_; }
}

Vec b = 10;
Vec c = a + b;
c[1] = 99;
```

The result is stored in `self`, except for comparisons (primitive), `[]` and `^` (reference). Copy, move and swap are generated field by field when not defined.

---

## Move and swap

`a <-- b` moves: `b` is left empty, and a pointer `b` becomes `nullptr`. A class move calls `op<--`.

```
op<--(mutable Buffer^ b) {
    delete data_;
    data_ <-- b^.data_;
}

Buffer dst <-- src;
x <--> y;                               // swap
```

---

## Inheritance

```
Animal(int legs_ = 4) {
    virtual void speak() = delete;      // pure virtual
}

Animal : Dog(int tricks_ = 0) {
    virtual void speak() {
        println(String + "woof " + legs_ + " " + tricks_);
    }
}

Dog dog(4, 2);                          // base fields first
Animal^ pet = ^dog;
pet^.speak();
```

Single inheritance only. `virtual` is required on every override. `Base:method()` calls the base version.

---

## Enums, aliases, namespaces, globals

```
enum Color ( kRed, kGreen, kBlue );
enum int64 Big ( kHuge = 5_000_000_000 );
Color c = kGreen;                       // the target type supplies the enum
x = Color:kBlue;

alias Pixel = (Color, int);

Shapes {
    int area(int w, int h) { return w * h; }
}
Shapes:area(2, 3);

global int count_ = 0;
global stats(int hits_ = 0) { }         // stats:hits_
```

---

## Templates

```
T biggest<T>(T a, T b) {
    if (a > b) { return a; }
    return b;
}
Pair<K, V>(K key_, V value_) { }

m = biggest(3, 9);                      // T inferred
Pair<char, int> p('a', 42);
Vector<int> v;                          // lib/hdr/vector.slh

alias Integers = <int|int8|int16|int32|int64>;
(const char)[] kind<T=Integers>(T arg) { return "integer"; }
(const char)[] kind<T=[N]>(T arg) { return "array"; }
(const char)[] kind<T>(T arg) { return "other"; }
```

---

## Files

A `.slh` header declares the public interface. Its `.sl` defines it. Everything else in a `.sl` is private.

```
// shape.slh
Shape(int w_, int h_, ...) {            // ... : more fields elsewhere
    _();
    ~();
    int area();
}

// shape.sl
import shape;
Shape(int scale_ = 1) {                 // private field; completes the class
    _() { }
    ~() { }
    int area() { return w_ * h_ * scale_; }
}

// C
int32 puts((const char)[] s) = import;
```

---

## Introspection

```
sizeof(int64)           // 8
sizeof("abc")           // 4
##type(x)  ##name(x)  ##func  ##file  ##line
```

---

## More information

The rules live in the comment at the top of each test file. That comment is the
spec; the code under it is the test.

- Function pointers: `bool cmp(int, int)^ = ^less;`
- Const correctness and `mutable`
- Casting: `<int^> p`
- Declaration with type conversion: `x int = y;`
- Pointer casting: `p Node^ = q;`
- Primitive temporaries: `int32(v)`
- String literals are `const char[N]`
- Field initialization vs assignment: `Point p(1, 2);` vs `Point p = (1, 2);`
- Empty init slots: `Point p(, 2);`
- Unnamed classes: `Point(1, 2);`
- Field aliases: `alias x = pos.x;` in a field list
- Expression optimization: temporaries, chains, when they die
- Overloads: functions, methods, operators
- Operators: `op+`, `op=`, `op<--`, `op[]`, template operators
- Class iteration: `for (x : container)` via `size`/`op[]` or `begin`/`end`/`next`
- Template parameter conventions, specialization and type sets
- Re-opening classes, incomplete classes, refinements
- Nested functions
- Globals and lazy global groups
- Enum context: bare members against an enum target
- The `void;` reachability marker
- Macros: `#x`, `##line`
- `import` vs `import+`
- Pre-link stage: template instantiation, `slidsc --instantiate`
- Makefile support: `slidsc --scan-deps`
- Origin story
