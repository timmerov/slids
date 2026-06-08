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
        type[] _$iter# = <type[]> ^tuple[0],
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
- by value (`x`) or by reference (`T^ p`); by value when ambiguous. The loop var's
  type must match the slot type T. The element type for a typeless `x` is inferred
  (the iterator/loop var are synthesized typeless and classify fills T).
- (todo: by-reference-to-const enforcement for a literal — needs const pointers
  [Phase 6]; rejecting a heterogeneous tuple LITERAL; non-primitive element types
  [Phase 5, forced by-ref].)
*/

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

    /*
    backing out of these until the for loops move to desugaring.
    where they are supposed to be.

    /* dereferenced */
    ref = ^tup;
    int s3 = 100;
    for (x : ref^) { s3 = s3 + x; }
    __println("s3= " + s3);                              // 113

    /* nested tuples */
    tuple = ((1,2), (3,4), (5,6));
    __println("tuple = (" + tuple[0][0]);
    for (sub : tuple) {
        __print("( ");
        for (x : sub^) {
            __print(x + " ");
        }
        __println(")");
    }
    __println(")");
    */

    return 0;
}
