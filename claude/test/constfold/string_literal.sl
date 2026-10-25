/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test operations involving string literals.

string literals are null terminated c strings.
they are read-only.

sizeof includes the trailing nul.

    sizeof("hello") // 6 = 5 letters + 1 nul

index read operations are allowed.
a constant index is range checked.

    "ab"[1] // 'b'
    "ab"[2] // nul

address of an indexed string literal is allowed.
type is a pointer to const char - (const char)[].

    ^"hello"[1] // address of the 'e'.

unary, binary, and comparison operations are not allowed.

    "hello" + 3;       // compile error.
    if (!"hello")      // compile error.
    if ("ab" == "ba")  // compile error.

string literals may be passed as (const char)[] parameters to
functions and methods including operators.

    void fn(const char[] s) {
        println(s);
    }
    fn("hello");

string literals may be used to initialize arrays and tuples.
the target must consume all of the characters but may omit the
trailing nul.
the target type does not have to be char. it must be able to
contain a char.

    char letters[5] = "hello";  // drops trailing nul.
    char text[6] = "hello";     // includes trailing nul.

    (h,e,l1,l2,o) = "hello";
    (h,e,l1,l2,o,z) = "hello";  // z=0

    int16 widestring[6] = "hello";
    int16 widechars[5] = "hello";

string literals may be assigned to pointers of type (const char)[].
it points to the first character of the string literal.

    const char[] hello = "hello";
    (const char)[] there = "there";

assignment of a string literal to a const char array is not a copy.
the variable is a pointer to the original constant characters.

    const char hello[5] = "hello";
    const char there[6] = "there";

the inferred type of a variable assigned to a string literal is a
pointer to the original constant characters.

    hello = "hello"; // const char hello[6] = "hello";

note: we may revisit the unary negation decision.
example:
    if ("")  // false
    if (!"") // true
*/

/*
claude says:

- a string literal is typed `const char[N]` (N counts the nul). it is STORAGE in the
  constant pool, so the operations that read storage work: sizeof, index reads,
  `^"lit"[i]`, decay to a `(const char)[]` parameter / pointer.
- a CONSTANT index folds to the char in constfold ("ab"[1] -> 'b'); one out of range
  is the ordinary array bounds error. a runtime index reads the pooled constant. an
  index under `^` never folds — it is an address, not a read.
- unary, binary, shift, and comparison operators reject a string literal operand
  (classify: the comparison arm, the unary arm, and the slot-wise explode every other
  aggregate operator goes through). a CLASS operator is not built-in: `String + "x"`
  dispatches on its class lhs and the literal reaches it through decay.
- an initializer / destructure whose target is not exactly `char[N]` sees the literal
  as N-1 (no nul) or N (with nul) char literals — a tuple literal — so the ordinary
  tuple -> array / destructure path widens each char (any type char widens into).
- ALIAS: a LOCAL const char array initialized from a literal (typed, N or N-1, or
  typeless `x = "lit"`) gets no storage: its address IS the pooled constant
  (Node::alias_literal; codegen registers the constant's symbol instead of an alloca).
  The pool dedups equal text, so two aliases of the same text share one address.
- for-loops over a literal stop before the nul (test/flow/forarray.sl).
*/

import string;
alias slids;

void fn(const char[] s) {
    println(s);
}

int32 main() {
    /* sizeof includes the nul. */
    println(String + "sizeof= " + sizeof("hello"));             // 6

    /* index reads: constant (folded) and runtime. */
    char b = "ab"[1];
    int nul = "ab"[2];
    println(String + "index= " + b + " nul= " + nul);           // b 0
    i = 2;
    println(String + "runtime index= " + "abc"[i]);             // c

    /* the address of an indexed literal is an iterator into it. */
    (const char)[] e = ^"hello"[1];
    println(String + "addr= " + e^ + e[1] + " " + ##type(e));   // el (const char)[]

    /* passed to a (const char)[] parameter, including a class operator. */
    fn("hello");                                                // hello
    println(String + "op= " + "there");                         // there

    /* initializers: N-1 drops the nul, N keeps it. */
    char letters[5] = "hello";
    char text[6] = "hello";
    int text_nul = text[5];
    println(String + "letters= " + letters[0] + letters[4]
        + " text nul= " + text_nul);                            // ho 0
    (h, e2, l1, l2, o) = "hello";
    println(String + "destructure= " + h + e2 + l1 + l2 + o);   // hello
    (h6, e6, l16, l26, o6, z) = "hello";
    int zi = z;
    println(String + "destructure z= " + zi + h6 + e6 + l16 + l26 + o6);  // 0hello

    /* any target type that holds a char. */
    int16 widestring[6] = "hello";
    int16 widechars[5] = "hello";
    println(String + "wide= " + widestring[0] + " " + widestring[5]
        + " " + widechars[4]);                                  // 104 0 111

    /* pointers point at the literal. */
    const char[] hello = "hello";
    (const char)[] there = "there";
    println(String + "ptr= " + hello[0] + there[4]);            // he

    /* a const char array ALIASES the literal: no copy, the same address. */
    const char alias5[5] = "alias";
    const char alias6[6] = "alias";
    inferred = "alias";
    println(String + "alias= " + alias5[0] + alias6[4] + inferred[1]
        + " " + ##type(inferred));                              // asl const char[6]
    println(String + "same address= " + (^alias5[0] == ^alias6[0])
        + " " + (^alias6[0] == ^inferred[0])
        + " " + (^inferred[0] == ^"alias"[0]));                 // true true true
    /* a non-const array is a copy. */
    char copy6[6] = "alias";
    copy6[0] = 'b';
    println(String + "copy= " + copy6[0] + " "
        + (^copy6[0] == ^"alias"[0]));                          // b false
    return 0;
}

/* a constant index is range checked. */
//-EXPECT-ERROR: Array index 3 is out of bounds for 'const char[3]'.
//int neg_index_range() { int c = "ab"[3]; return c; }

/* unary, binary, and comparison operators reject a string literal. */
//-EXPECT-ERROR: Operator '+' is not defined on a string literal.
//int neg_binary() { x = "hello" + 3; int r = x[0]; return r; }

//-EXPECT-ERROR: Operator '+' is not defined on a string literal.
//int neg_binary_char() { char c = 'A'; x = " " + c; int r = x[0]; return r; }

//-EXPECT-ERROR: Operator '+' is not defined on a string literal.
//int neg_binary_lit_lit() { x = "ab" + "cd"; int r = x[0]; return r; }

//-EXPECT-ERROR: Operator '!' is not defined on a string literal.
//int neg_not() { if (!"hello") { return 1; } return 0; }

//-EXPECT-ERROR: Operator '-' is not defined on a string literal.
//int neg_negate() { x = -"ab"; int r = x[0]; return r; }

//-EXPECT-ERROR: Operator '<<' is not defined on a string literal.
//int neg_shift() { x = "ab" << 1; int r = x[0]; return r; }

//-EXPECT-ERROR: Operator '==' is not defined on a string literal.
//int neg_compare() { if ("ab" == "ba") { return 1; } return 0; }

//-EXPECT-ERROR: Operator '+=' is not defined on a string literal.
//int neg_aug() { char buf[3] = "ab"; buf += "cd"; int r = buf[0]; return r; }

/* an initializer must consume every character. */
//-EXPECT-ERROR: Cannot assign 'const char[6]' to 'char[4]'; array shape differs.
//int neg_short() { char x[4] = "hello"; int r = x[0]; return r; }

//-EXPECT-ERROR: Cannot assign 'const char[6]' to 'char[7]'; array shape differs.
//int neg_long() { char x[7] = "hello"; int r = x[0]; return r; }

//-EXPECT-ERROR: Destructure has 4 target(s)
//int neg_destructure_short() { (a, b, c, d) = "hello"; int r = a; r = b; r = c; r = d; return r; }

/* the target must hold a char. */
//-EXPECT-ERROR: Cannot implicitly convert 'char' to 'int8'
//int neg_int8() { int8 x[6] = "hello"; return x[0]; }

/* an alias is read-only. */
//-EXPECT-ERROR: Cannot write to a const value.
//int neg_alias_write() { const char x[6] = "hello"; x[0] = 'j'; int r = x[0]; return r; }
