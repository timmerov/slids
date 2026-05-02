/*
develop the ranged tuple

only exists in a for statement.

format:

    <start> .. <comp> <end> <op> <step>

    start, end, step are expressions.
    comp is a comparison operation.
    op is a simple math operation: +,-,*,/

    start and end are required. all others are optional.
    expressions need to be enclosed in parentheses to
    avoid vexing parses.

    expressions are evaluated once at initialization.
    expressions must be integer type, int or wider;
    int32, uint32, int64, uint64.

desugars to long for syntax:

    // keyword
    for
        // initializeation tuple
        (__index = start, __end = end, __step = step)
        // condition tuple
        (__index cmp __end)
        // update block
        { __index = __index op step }
        // loop body
        { }

checking for malformed ranged tuples that loop
infinitely, is defered to the author.

TODO:
check that ranged tuples composed of integer literals
do not loop forever.
*/

int32 main() {

    /* ============================================================
       A. Comparison sweep — default type int, default step +1.
       ============================================================ */

    /* A1: bare .. — default cmp is exclusive <. */
    /* expect: 0 1 2 */
    for x in (0..3) { __print(x + " "); }
    __println();

    /* A2: ..< — explicit exclusive (equivalent to A1). */
    /* expect: 0 1 2 */
    for x in (0..<3) { __print(x + " "); }
    __println();

    /* A3: ..<= — inclusive. */
    /* expect: 0 1 2 3 */
    for x in (0..<=3) { __print(x + " "); }
    __println();

    /* A4: ..> — descending exclusive (explicit step -1; default +1 would loop forever). */
    /* expect: 3 2 1 */
    for x in (3..>0-1) { __print(x + " "); }
    __println();

    /* A5: ..>= — descending inclusive (explicit step -1). */
    /* expect: 3 2 1 0 */
    for x in (3..>=0-1) { __print(x + " "); }
    __println();

    /* A6: ..!= — exclusive, terminate on equality. */
    /* expect: 0 1 2 */
    for x in (0..!=3) { __print(x + " "); }
    __println();

    /* ============================================================
       B. Step-op sweep — default type int, default cmp <.
       Each test isolates a step op AND a direction
       (the two are correlated; user enforces consistency).
       ============================================================ */

    /* B1: default step (+1). */
    /* expect: 0 1 2 3 4 5 */
    for x in (0..6) { __print(x + " "); }
    __println();

    /* B2: explicit + step. */
    /* expect: 0 2 4 */
    for x in (0..<6+2) { __print(x + " "); }
    __println();

    /* B3: explicit - step (descending). */
    /* expect: 6 4 2 */
    for x in (6..>0-2) { __print(x + " "); }
    __println();

    /* B4: explicit * step (powers of 2). */
    /* expect: 1 2 4 8 16 32 64 */
    for x in (1..<=64*2) { __print(x + " "); }
    __println();

    /* B5: explicit / step (descending log). */
    /* expect: 64 32 16 8 4 2 1 */
    for x in (64..>=1/2) { __print(x + " "); }
    __println();

    /* ============================================================
       C. Type sweep — int defaults are exercised by all earlier
       tests; here we cover the non-default integer types.
       ============================================================ */

    /* C1: int64. */
    /* expect: 0 1 2 3 4 */
    int64 c1_lo = 0;
    int64 c1_hi = 5;
    for x in (c1_lo..c1_hi) { __print(x + " "); }
    __println();

    /* C2: uint32. */
    /* expect: 0 1 2 3 4 */
    uint32 c2_lo = 0;
    uint32 c2_hi = 5;
    for x in (c2_lo..c2_hi) { __print(x + " "); }
    __println();

    /* C3: uint64. */
    /* expect: 0 1 2 3 4 */
    uint64 c3_lo = 0;
    uint64 c3_hi = 5;
    for x in (c3_lo..c3_hi) { __print(x + " "); }
    __println();

    /* C4: int8 (explicit narrow). */
    /* expect: 0 1 2 3 4 */
    int8 c4_lo = 0;
    int8 c4_hi = 5;
    for x in (c4_lo..c4_hi) { __print(x + " "); }
    __println();

    /* C5: uint8 (explicit narrow). */
    /* expect: 0 1 2 3 4 */
    uint8 c5_lo = 0;
    uint8 c5_hi = 5;
    for x in (c5_lo..c5_hi) { __print(x + " "); }
    __println();

    /* C6: literal widening — int8 lo, int literal end widens to int8. */
    /* expect: 0 1 2 3 4 */
    int8 c6_lo = 0;
    for x in (c6_lo..5) { __print(x + " "); }
    __println();

    /* ============================================================
       D. Bound and step expressions evaluated once.
       Use a Counter slid; verify counter == 1 after the loop.
       ============================================================ */

    Counter d_bound;
    Counter d_step;

    /* D1: bound expression evaluated exactly once. */
    /* expect: 0 1 2 3 4 / d_bound.n_=1 */
    for x in (0..d_bound.next_5()) { __print(x + " "); }
    __print("/ d_bound.n_=" + d_bound.n_); __println();

    /* D2: step expression evaluated exactly once. */
    /* expect: 0 2 4 / d_step.n_=1 */
    for x in (0..<5+d_step.next_2()) { __print(x + " "); }
    __print("/ d_step.n_=" + d_step.n_); __println();

    /* ============================================================
       G. Empty / single-iteration boundary cases.
       ============================================================ */

    /* G1: empty ascending — start == end, exclusive cmp. */
    /* expect: <empty> */
    for x in (5..<5) { __print(x + " "); }
    __println();

    /* G2: empty descending. */
    /* expect: <empty> */
    for x in (0..>0) { __print(x + " "); }
    __println();

    /* G3: single-iteration — start == end, inclusive. */
    /* expect: 5 */
    for x in (5..<=5) { __print(x + " "); }
    __println();

    /* ============================================================
       H. Vexing-parse paren rule.
       The two reads of "0..<=10+2" are distinguished by parens
       around the end expression.
       ============================================================ */

    /* H1: parens around end — end=12, default step +1. */
    /* expect: 0 1 2 3 4 5 6 7 8 9 10 11 12 */
    for x in (0..<=(10+2)) { __print(x + " "); }
    __println();

    /* H2: no parens — end=10, step+2. */
    /* expect: 0 2 4 6 8 10 */
    for x in (0..<=10+2) { __print(x + " "); }
    __println();

    /* ============================================================
       N. Nested for loops — exercises unique synthetic-end naming
       and loop-var save/restore (shadowing).
       ============================================================ */

    /* N1: distinct loop vars, distinct bound types. */
    /* expect: i=0[j=0,j=1,j=2] i=1[j=0,j=1,j=2] */
    for i in (0..2) {
        __print("i=" + i + "[");
        int64 n1_lo = 0;
        int64 n1_hi = 3;
        for j in (n1_lo..n1_hi) {
            __print("j=" + j);
            if (j != 2) { __print(","); }
        }
        __print("] ");
    }
    __println();

    /* N2: same loop-var name in outer and inner — inner shadows outer,
       outer's value is restored each iteration. */
    /* expect: o=0(s=0 s=1) o=1(s=0 s=1) o=2(s=0 s=1) */
    for x in (0..3) {
        __print("o=" + x + "(");
        for x in (0..2) { __print("s=" + x + " "); }
        __print(") ");
    }
    __println();

    /* N3: nested with explicit cmp/step on both loops. */
    /* expect: 4>2>0> 4>2>0> 4>2>0> */
    for i in (0..<=2) {
        for j in (4..>=0-2) { __print(j + ">"); }
        __print(" ");
    }
    __println();

    /* ============================================================
       Compile-error catalog (verified one at a time).
       Per the catalog workflow: uncomment one line, build,
       confirm the error, restore the comment.
       ============================================================ */

    /* I. Range only valid inside a for. */
    /* I1: range expression used outside a for-iterator. */
    // int range_outside = (0..10);

    /* J. Type errors. */
    /* J1: width mismatch start vs end. */
    // int32 j1_lo = 0; int64 j1_hi = 5;
    // for x in (j1_lo..j1_hi) { }

    /* J2: sign mismatch start vs end. */
    // int32 j2_lo = 0; uint32 j2_hi = 5;
    // for x in (j2_lo..j2_hi) { }

    /* J3: step type mismatch. */
    // int32 j3_lo = 0; int32 j3_hi = 10; int64 j3_step = 2;
    // for x in (j3_lo..<j3_hi+j3_step) { }

    /* J4: float type rejected. */
    // for x in (0.0..<1.0) { }

    /* J5: missing end. */
    // for x in (0..) { }

    /* K. Malformed step. */
    /* K1: step value with no op. */
    // for x in (0..<10 2) { }

    /* K2: op with no step value. */
    // for x in (0..<10+) { }

    /* K3: chained ops — only one allowed. */
    // for x in (0..<10+2*3) { }

    return 0;
}

/*
Counter helper for D-section eval-once tests.
*/
Counter(int n_ = 0) {
    int next_5() {
        n_ = n_ + 1;
        return 5;
    }
    int next_2() {
        n_ = n_ + 1;
        return 2;
    }
}
