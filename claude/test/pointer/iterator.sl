/*
test iterators.

iterators are a type of pointer.
they point to an iterable sequence of objects.
iterators may be assigned, re-assigned, and set to nullptr.
you may do additive math operations on iterators.
including augmented additive math operations.

square brackets [] after a type indicates an iterator.
caret ^ after an iterator type variable is the object.
caret ^ before an indexed array variable is an iterator to the indexed object.

    int[] iter = nullptr;
    int arr[5];
    iter = ^arr[3];
    int y = iter^;

    (const char)[] str = "Hello, World!";
    char[] e = ^str[1];
    char[] r = e + 8;
    ++e;
    --r;
    intptr diff = r - e;
    e += 3;
    r -= 2;

iterators as addresses are incremented by the size of the object.
the difference between to iterators is the difference between
the addresses divided by the size of the object.

pointers may be compared to other pointers where the object is the same type.
iterators may be compared to references, and vice versa.
comparison operations: == != <= >= < >
*/

/*
claude says:

- an iterator is `T[]` — a pointer into an iterable sequence (an LLVM `ptr`).
  `^arr[i]` (array element address) or a `char[]` string literal seeds one.
- `iter^` derefs; `iter[i]` subscripts (element stride); `^iter[i]` is an
  iterator to that element.
- additive arithmetic steps by element: `iter + n`, `iter - n`, `iter += n`,
  `iter -= n`, `++`, `--`. The difference `iter - iter` is in elements (`intptr`).
- iterators compare with all six ops against a same-pointee pointer.
*/

import string;

int32 main() {
    /* an int iterator seeded from an array element. */
    int arr[5];
    for (i : 0..5) {
        arr[i] = i * i;            // 0, 1, 4, 9, 16
    }
    int[] it = ^arr[1];
    println(String + "it^= " + it^);              // 1

    /* additive arithmetic steps by element. */
    int[] it3 = it + 2;
    println(String + "it3^= " + it3^);            // arr[3] = 9
    println(String + "it[2]= " + it[2]);          // arr[3] = 9

    /* ++ / -- step one element. */
    ++it;
    println(String + "it^= " + it^);              // arr[2] = 4
    --it;
    println(String + "it^= " + it^);              // arr[1] = 1

    /* += / -= step by n elements in place. */
    it += 2;
    println(String + "it^= " + it^);              // arr[3] = 9
    it -= 2;
    println(String + "it^= " + it^);              // arr[1] = 1

    /* the difference is in elements. */
    intptr d = it3 - it;
    println(String + "d= " + d);                  // 2

    /* comparison — array-element addresses are ordered. */
    println(String + "it<it3= " + (it < it3));    // true
    println(String + "it==it3= " + (it == it3));  // false
    println(String + "it>=it= " + (it >= it));    // true

    /* a char iterator from a string literal. */
    (const char)[] str = "Hello, World!";
    (const char)[] e = ^str[1];
    println(String + "e^= " + e^);                // e
    (const char)[] r = e + 8;
    println(String + "r^= " + r^);                // r  (index 9)
    ++e;
    --r;
    println(String + "e^= " + e^);                // l  (index 2)
    println(String + "r^= " + r^);                // o  (index 8)
    intptr diff = r - e;
    println(String + "diff= " + diff);            // 6

    /* iter - n and int + iter (additive arithmetic, both directions). */
    int[] back = it3 - 1;                  // arr[3] -> arr[2]
    println(String + "back^= " + back^);          // 4
    int[] fwd = 1 + it;                    // arr[1] -> arr[2]
    println(String + "fwd^= " + fwd^);            // 4

    /* iterator subscript as an lvalue — write through it. */
    int[] w = ^arr[0];
    w[2] = 100;
    println(String + "arr[2]= " + arr[2]);        // 100

    /* a null iterator compares to nullptr. */
    int[] none = nullptr;
    println(String + "none==nullptr= " + (none == nullptr));  // true
    println(String + "none!=nullptr= " + (none != nullptr));  // false

    /* A STRING LITERAL DECAYS like any other array. Its type is `const char[N]`
       (lex/literal.sl owns that), so reaching a `char[]` is the ordinary
       array -> element-pointer decay — the same rung an `int[3]` takes to `int[]`,
       not a rule of its own. `str` above is already this; these pin the OTHER
       destinations the decay has to satisfy. */

    /* to a REFERENCE (`char^`), not just an iterator. */
    (const char)^ cref = "abc";
    println(String + "cref^= " + cref^);          // a

    /* through a PARAMETER — the case every string-taking function depends on. A
       non-mutable iterator param munges to `(const char)[]`, and the decayed literal
       matches it. */
    println(String + "first= " + firstChar("xyz"));      // x
    println(String + "len= " + litLen("hello"));         // 5

    /* the decayed pointer is an ordinary iterator: arithmetic and comparison apply. */
    (const char)[] lit = "abcdef";
    println(String + "lit[3]= " + lit[3]);        // d
    (const char)[] lend = lit + 5;
    println(String + "lend^= " + lend^);          // f
    println(String + "litdiff= " + (lend - lit)); // 5

    /* two literals are distinct storage, so their addresses differ; a literal is
       never null. */
    println(String + "lit!=null= " + (lit != nullptr));  // true

    return 0;
}

/* the parameters the decay must satisfy: an iterator and a reference. Both are
   non-mutable, so both munge to a const pointee — a literal is read-only storage
   and this is the shape that will enforce it when const enforcement lands. */
char firstChar(char[] s) {
    return s[0];
}

intptr litLen(char[] s) {
    intptr n = 0;
    for () (s[n]) { ++n; } {}
    return n;
}

/* iterators support only additive arithmetic — '*' is rejected. */
//-EXPECT-ERROR: Arithmetic is not defined on a pointer.
//int neg_mul() {
//    int arr[5];
//    int[] it = ^arr[0];
//    int[] bad = it * 2;
//    return bad^;
//}

/* compound assignment is additive-only — '*=' is rejected (like '*'). */
//-EXPECT-ERROR: Arithmetic is not defined on a pointer.
//int neg_mul_assign() {
//    int arr[5];
//    int[] it = ^arr[0];
//    it *= 2;
//    return it^;
//}

/* adding two iterators is not defined. */
//-EXPECT-ERROR: Arithmetic is not defined on a pointer.
//int neg_add_iters() {
//    int arr[5];
//    int[] a = ^arr[0];
//    int[] b = ^arr[1];
//    int[] bad = a + b;
//    return bad^;
//}

/* the difference of iterators with different pointee types is rejected. */
//-EXPECT-ERROR: Pointer subtraction requires the same pointee type.
//intptr neg_diff_pointee() {
//    int arr[5];
//    (const char)[] s = "hi";
//    int[] a = ^arr[0];
//    intptr d = a - s;
//    return d;
//}

/* storing through an uninitialized iterator is use-before-initialization
   (the pointer is read to compute the element address). */
//-EXPECT-ERROR: Use of uninitialized variable 'w'.
//int neg_store_uninit_iter() {
//    int[] w;
//    w[0] = 1;
//    return 0;
//}
