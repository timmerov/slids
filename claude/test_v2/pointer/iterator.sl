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

    char[] str = "Hello, World!";
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
- additive arithmetic steps by element: `iter + n`, `iter - n`, `++`, `--`. The
  difference `iter - iter` is in elements (`intptr`).
- iterators compare with all six ops against a same-pointee pointer.
*/

int32 main() {
    /* an int iterator seeded from an array element. */
    int arr[5];
    for (i : 0..5) {
        arr[i] = i * i;            // 0, 1, 4, 9, 16
    }
    int[] it = ^arr[1];
    __println("it^= " + it^);              // 1

    /* additive arithmetic steps by element. */
    int[] it3 = it + 2;
    __println("it3^= " + it3^);            // arr[3] = 9
    __println("it[2]= " + it[2]);          // arr[3] = 9

    /* ++ / -- step one element. */
    ++it;
    __println("it^= " + it^);              // arr[2] = 4
    --it;
    __println("it^= " + it^);              // arr[1] = 1

    /* the difference is in elements. */
    intptr d = it3 - it;
    __println("d= " + d);                  // 2

    /* comparison — array-element addresses are ordered. */
    __println("it<it3= " + (it < it3));    // true
    __println("it==it3= " + (it == it3));  // false
    __println("it>=it= " + (it >= it));    // true

    /* a char iterator from a string literal. */
    char[] str = "Hello, World!";
    char[] e = ^str[1];
    __println("e^= " + e^);                // e
    char[] r = e + 8;
    __println("r^= " + r^);                // r  (index 9)
    ++e;
    --r;
    __println("e^= " + e^);                // l  (index 2)
    __println("r^= " + r^);                // o  (index 8)
    intptr diff = r - e;
    __println("diff= " + diff);            // 6

    /* iter - n and int + iter (additive arithmetic, both directions). */
    int[] back = it3 - 1;                  // arr[3] -> arr[2]
    __println("back^= " + back^);          // 4
    int[] fwd = 1 + it;                    // arr[1] -> arr[2]
    __println("fwd^= " + fwd^);            // 4

    /* iterator subscript as an lvalue — write through it. */
    int[] w = ^arr[0];
    w[2] = 100;
    __println("arr[2]= " + arr[2]);        // 100

    /* a null iterator compares to nullptr. */
    int[] none = nullptr;
    __println("none==nullptr= " + (none == nullptr));  // true
    __println("none!=nullptr= " + (none != nullptr));  // false

    return 0;
}

/* iterators support only additive arithmetic — '*' is rejected. */
//-EXPECT-ERROR: Arithmetic is not defined on a pointer.
//int neg_mul() {
//    int arr[5];
//    int[] it = ^arr[0];
//    int[] bad = it * 2;
//    return bad^;
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
//    char[] s = "hi";
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
