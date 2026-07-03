/*
test for loop over a tuple.

the tuple must be homogeneous.
the type of the loop variable must match.
loop by value or reference.
by value in case of ambiguity.
if the type is not primitive, then must be by reference.

by value:

    for (x : (7, 4, 2) { }

    tuple = (1,3,9);
    for (x : tuple) { }

by reference to mutable or const for tuple variables,
by reference to const for tuple literals:

    int sum = 0;
    for (int^ p : (-1, -2, +3)) {
        sum += p^;
    }

nested tuples loop by reference.

    tuple = ((1,2), (3,4), (5,6));
    for (sub : tuple) {
        for (x : sub^) {
            __print(x + " ");
        }
        __println();
    }

desugars to:

    for (
        intptr _$idx# = 0,
        type[] _$iter# = <type[]> ^tuple[0],
        type val
    ) (
        _$idx# < tuple._$size
    ) {
        ++_$idx#;
        ++_$iter#;
    } {
        val = _$iter#^;
        /*body*/
    }

    for (
        intptr _$idx# = 0,
        type[] _$iter# = <type[]> <void> ^tuple[0],
        type^ ref
    ) (
        _$idx# < tuple._$size
    ) {
        ++_$idx#;
        ++_$iter#;
    } {
        ref = _$iter#;
        /*body*/
    }
*/

/*
claude says:

- `for (v : tuple) {body}` iterates a HOMOGENEOUS tuple (all slots one type T).
  Lowers (like for-array) to a kForLongStmt that walks an iterator: a counter
  `_$idx < N` (static arity) alongside `_$iter = ^tuple[0]`, both stepped each
  pass; the body binds the loop var from `_$iter` (`v = _$iter^` by value,
  `v = _$iter` by reference — the iterator demotes to the reference).
- The iterable is a tuple LITERAL (`for (x : (7,4,2))`) or a tuple VARIABLE
  (`for (x : tuple)`). A variable is iterated IN PLACE — no copy — so a by-mutable-
  reference loop var writes back to it. A literal is spilled to a temp.
- by value (`x`) or by reference (`T^ p`); by value when ambiguous. A NON-PRIMITIVE
  slot type (a nested tuple, a class) FORCES a reference — there is no by-value copy;
  the by-ref var aliases the slot in place (`sub^` re-iterates a nested tuple, `ref^.x_`
  reads a class slot). The loop var's type must match the slot type T; a typeless `x`
  infers it (iterator + loop var are synthesized typeless and classify fills T).
- (todo: by-reference-to-const enforcement for a literal — needs const pointers
  [Phase 6]; rejecting a heterogeneous tuple LITERAL.)
*/

/* a function returning a tuple — for the rvalue-spill case. */
(int, int, int) make_tuple() {
    return (4, 5, 6);
}

int32 main() {

    /* by value, tuple literal */
    int s1 = 0;
    for (x : (7, 4, 2)) { s1 = s1 + x; }
    __println("s1= " + s1);                              // 13

    /* by value, tuple variable */
    (int, int, int) tup = (1, 3, 9);
    int s2 = 0;
    for (x : tup) { s2 = s2 + x; }
    __println("s2= " + s2);                              // 13

    /* by reference (to const) over a literal */
    int sum = 0;
    for (int^ p : (-1, -2, +3)) { sum += p^; }
    __println("sum= " + sum);                            // 0

    /* by mutable reference over a variable — writes back in place */
    (int, int, int) m = (1, 2, 3);
    for (int^ p : m) { p^ = p^ * 10; }
    __println("m= " + m[0] + " " + m[1] + " " + m[2]);   // 10 20 30

    /* inferred type. */
    tpl1 = (1,2,4);
    __print("tpl1 = ( ");
    for (x : tpl1) {
        __print(x+" ");
    }
    __println(")");

    /* inferred reference */
    tpl2 = (88,89,90);
    ref2 = ^tpl2;
    __print("tpl1 = ( ");
    for (x : ref2^) {
        __print(x+" ");
    }
    __println(")");

    /* nested tuples */
    tpl3 = ((1,2), (3,4), (5,6));
    __println("tpl3 = (");
    for (sub : tpl3) {
        __print("  ( ");
        for (x : sub^) {
            __print(x + " ");
        }
        __println(")");
    }
    __println(")");

    /* break exits a tuple loop. */
    (int, int, int) bt = (1, 2, 3);
    int bsum = 0;
    for (int^ p : bt) {
        if (p^ == 2) { break; }
        bsum = bsum + p^;
    }
    __println("break= " + bsum);                 // 1

    /* continue skips an element. */
    int csum = 0;
    for (int^ p : bt) {
        if (p^ == 2) { continue; }
        csum = csum + p^;
    }
    __println("continue= " + csum);              // 1 + 3 = 4

    /* a labeled break from the inner tuple loop exits the OUTER. */
    int lb = 0;
    for (int^ a : bt) {
        for (int^ b : bt) {
            lb = lb + a^;
            if (b^ == 2) { break scan; }
        }
    } :scan;
    __println("labeled_break= " + lb);           // 2

    /* a numbered break exits both tuple loops. */
    int nb = 0;
    for (int^ a : bt) {
        for (int^ b : bt) {
            nb = nb + a^;
            if (b^ == 2) { break 2; }
        }
    }
    __println("numbered_break= " + nb);          // 2

    /* a labeled continue restarts the OUTER tuple loop. */
    int lc = 0;
    for (int^ a : bt) {
        for (int^ b : bt) {
            if (b^ == 2) { continue outer; }
            lc = lc + a^;
        }
    } :outer;
    __println("labeled_continue= " + lc);        // 1 + 2 + 3 = 6

    /* a typeless loop var reuses an enclosing local — observable after. */
    int rlast = 0;
    for (rlast : bt) {
    }
    __println("reuse= " + rlast);                // 3

    /* a typed by-value loop var (the cases above use typeless by value). */
    int tv = 0;
    for (int x : bt) {
        tv = tv + x;
    }
    __println("typed_byval= " + tv);             // 6

    /* a char-element tuple, by reference. */
    (char, char, char) ct = ('a', 'b', 'c');
    int chsum = 0;
    for (char^ p : ct) {
        chsum = chsum + p^;
    }
    __println("char= " + chsum);                 // 294

    /* a float-element tuple, by value. */
    (float, float, float) ft = (1.5, 2.5, 3.0);
    float fsum = 0.0;
    for (float f : ft) {
        fsum = fsum + f;
    }
    __println("float= " + fsum);                 // 7

    /* an rvalue (a function call) is SPILLED to a temp, then iterated. */
    int spill = 0;
    for (x : make_tuple()) {
        spill = spill + x;
    }
    __println("call_spill= " + spill);           // 15

    /* a const-EXPRESSION dim in the FOR-VAR's TYPE (a tuple slot): iterate a tuple
       whose slots are themselves (int[kFN], int); the by-ref loop var's dim is
       folded + baked onto the loop-var decl. */
    const int kFN = 3;
    ((int[kFN], int), (int[kFN], int)) pairs = (((1,2,3), 10), ((4,5,6), 20));
    for ((int[kFN], int)^ e : pairs) {
        __println("pair= " + e^[0][2] + " " + e^[1]);   // 3 10  then  6 20
    }

    {
        Class(int x_) { }
        tuple = (Class(1), Class(2), Class(3));
        __print("tuple = (");
        for (ref : tuple) {
            __print(" " + ref^.x_);
        }
        __println(" )");
    }

    return 0;
}

/*
negatives — one //-block uncommented per run.
*/

/* a heterogeneous tuple cannot be iterated (the iterator strides by slot 0's type). */
//-EXPECT-ERROR: requires a homogeneous tuple
//int neg_heterogeneous() {
//    for (x : (1, 2.0, 3)) {
//        __println("" + x);
//    }
//    return 0;
//}

/* a non-primitive element (a tuple slot) must be iterated by reference. */
//-EXPECT-ERROR: must use a reference loop variable
//int neg_nonprimitive_byvalue() {
//    alias Pair = (int, int);
//    t = ((1, 2), (3, 4));
//    for (Pair sub : t) {
//        __println("" + sub[0]);
//    }
//    return 0;
//}

/* a by-reference loop variable's pointee must match the element type. */
//-EXPECT-ERROR: does not match the tuple element type
//int neg_byref_mismatch() {
//    (int, int, int) t = (1, 2, 3);
//    for (int64^ p : t) {
//        p^ = 0;
//    }
//    return 0;
//}

/* the iterable must be a tuple (or enum / array), not a scalar. */
//-EXPECT-ERROR: is not an enum, array, or tuple
//int neg_not_tuple() {
//    int v = 5;
//    for (x : v) {
//        __println("" + x);
//    }
//    return 0;
//}
