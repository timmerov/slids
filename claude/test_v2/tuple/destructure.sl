/*
test destructuring tuples.

    (var-list) = tuple

example:

    int a;
    int b;
    {
        int c;
        (a, int b, int c, d, ) = (1,2,3,4,5);
    }

    a is re-used.
    b is a new variable.
    local b shadows b from the enclosing scope.
    c already exists in this scope - compile error.
    d is inferred.
    the 5th slot value is dropped.

*/

/*
claude says:

a destructure binds a tuple's slots to a parenthesized list of DECLARATOR slots.
each non-empty slot is `[type] name`; an empty slot (a bare `,`) DISCARDS its
tuple element. dispatched by type-presence then lexical lookup:

  - TYPED slot (`int x`): declares a fresh local of that type.
  - UNTYPED slot (`x`): REUSES an existing variable up the scope chain (a store);
    if none, DECLARES a fresh local whose type is inferred from the tuple element.
  - empty slot: the tuple element is dropped.

arity must match, INCLUDING discards. the rhs must be a tuple; a typed slot or a
reused variable must already match its tuple element's type.

deferred: NESTED destructure `((a,b),c) = ...`, the typed-slot same-scope DUP
error (`int c` when c is in this scope), and the `(named slots) name` "too many
names" shape rule.
*/

(int, int) makePair() {       // a function returning a tuple — a destructure rhs
    return (11, 22);
}

int32 main() {

    /* untyped slots REUSE existing variables (a store). */
    int a = 0;
    int b = 0;
    (a, b) = (10, 20);
    __println("reuse: a= " + a + " b= " + b);              // 10 20

    /* untyped slots that don't exist DECLARE, type inferred from the slot. */
    (c, d) = (30, 40);
    __println("declare: c= " + c + " d= " + d);            // 30 40

    /* TYPED slots declare a fresh variable of the written type. */
    (int x, int y) = (50, 60);
    __println("typed: x= " + x + " y= " + y);              // 50 60

    /* mixed: reuse a, declare-typed e, declare-inferred f. */
    (a, int e, f) = (70, 80, 90);
    __println("mixed: a= " + a + " e= " + e + " f= " + f); // 70 80 90

    /* a trailing empty slot DISCARDS its tuple element. */
    int g = 0;
    (g, ) = (100, 200);
    __println("discard-tail: g= " + g);                    // 100

    /* an interior empty slot discards too. */
    int h = 0;
    int k = 0;
    (h, , k) = (1, 2, 3);
    __println("discard-mid: h= " + h + " k= " + k);        // 1 3

    /* destructure from a tuple VARIABLE, not only a literal. */
    (int, int) pr = (7, 8);
    (int m, int n) = pr;
    __println("from-var: m= " + m + " n= " + n);           // 7 8

    /* HETEROGENEOUS slots — the tuple's slots have different types. */
    (int hx, bool hy, float hz) = (1, true, 2.5);
    __println("hetero: hx= " + hx + " hy= " + hy + " hz= " + hz);   // 1 true 2.5

    /* the rhs may be a FUNCTION returning a tuple. */
    (int fa, int fb) = makePair();
    __println("func: fa= " + fa + " fb= " + fb);           // 11 22

    /* the rhs is evaluated ONCE, so a self-referential destructure SWAPS. */
    int sa = 1;
    int sb = 2;
    (sa, sb) = (sb, sa);
    __println("swap: sa= " + sa + " sb= " + sb);           // 2 1

    /* enclosing scope: an untyped slot REUSES an outer-scope variable; a TYPED
       slot declares a fresh local that SHADOWS the outer one. */
    int oa = 1;
    int ob = 2;
    {
        (oa, int ob) = (10, 20);
        __println("inner: oa= " + oa + " ob= " + ob);      // 10 20
    }
    __println("outer: oa= " + oa + " ob= " + ob);          // 10 2

    /* an ARRAY is a homogeneous tuple, so it destructures too. */
    int arr[2] = (10, 20);
    (int aa, int ab) = arr;
    __println("array: aa= " + aa + " ab= " + ab);          // 10 20

    return 0;
}

/* a REUSED variable whose type doesn't match its tuple element is rejected too
   (the same check as a typed slot, on the existing variable's type). */
//-EXPECT-ERROR: but slot 0 is 'int'
//int32 neg_reuse_mismatch() {
//    int8 rm = 0;
//    (rm, ) = (100, 200);
//    return rm;
//}

/* compile errors — each uncommented in isolation by the negative runner. */

/* the right side must be a tuple. */
//-EXPECT-ERROR: The right side of a destructure must be a tuple
//int32 neg_not_tuple() {
//    int p = 0; int q = 0;
//    (p, q) = 5;
//    return p + q;
//}

/* arity must match (including discards). */
//-EXPECT-ERROR: Destructure has 2 target(s) but the tuple
//int32 neg_arity() {
//    int p = 0; int q = 0;
//    (p, q) = (1, 2, 3);
//    return p + q;
//}

/* a typed slot or a reused variable must match its tuple element's type. */
//-EXPECT-ERROR: but slot 0 is 'int'
//int32 neg_type_mismatch() {
//    (int8 p, int q) = (1, 2);
//    return p + q;
//}
