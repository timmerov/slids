/*
infer types of arrays.
and other things.

slids reading: leftmost type-bracket is innermost; rightmost is outermost.
arr[A][B] reads as (arr[A])[B] — A is the inner bound, B is the outer.
arr[i][j] reads outer-first by index: i innermost, j outermost.
row-major flat: flat = j * A + i. outer index changes slowest.

structural init: rhs shape must match dims; outer arity = dims[n-1],
each inner slot a tuple of dims[n-2], etc. tail-short at the outer
level zero-fills; inner level mismatches are errors.
scalar rhs to a fixed-size array is rejected unless total == 1.
*/

Cell(int v_ = 0) {}

int32 main() {

    a_ray[3] = (1, 2, 3);
    __print("a_ray type: " + ##type(a_ray));
    __print("a_ray[1,2,3]=");
    for (a : a_ray) {
        __print(" " + a);
    }
    __println();

    b_ray[2][3] = (
        (10, 11),
        (12, 13),
        (14, 15)
    );
    for (y : 0..3) {
        __print("b_ray[" + y + "]=");
        for (x : 0..2) {
            __print(" " + b_ray[x][y]);
        }
        __println();
    }

    /* 3D array. dims=[2,3,4]: innermost 2, middle 3, outermost 4.
       flat = k*6 + j*2 + i for cube[i][j][k]. */
    int cube[2][3][4] = (
        ((10, 11), (12, 13), (14, 15)),
        ((20, 21), (22, 23), (24, 25)),
        ((30, 31), (32, 33), (34, 35)),
        ((40, 41), (42, 43), (44, 45))
    );
    __println("cube[0][0][0]=" + cube[0][0][0]);   /* expect 10 */
    __println("cube[1][2][3]=" + cube[1][2][3]);   /* expect 45 */

    /* size-1 array: scalar rhs promotes to a 1-tuple. */
    int one[1] = 5;
    __println("one[0]=" + one[0]);                 /* expect 5 */

    /* tail-short at the outer dim of a multi-dim array: only outer
       slot 0 is provided; outer slots 1 and 2 zero-fill. */
    int short_grid[2][3] = ((1, 2));
    for (j : 0..3) {
        __print("short_grid[?][" + j + "]=");
        for (i : 0..2) {
            __print(" " + short_grid[i][j]);
        }
        __println();
    }
    /* expect: row 0: 1 2 ; row 1: 0 0 ; row 2: 0 0 */

    /* inferred elem type with slid-typed slot values. */
    inferred_cells[2] = (Cell(7), Cell(11));
    __println("inferred_cells[0].v_=" + inferred_cells[0].v_);
    __println("inferred_cells[1].v_=" + inferred_cells[1].v_);

    /* negative: scalar rhs to a fixed-size array with total > 1. */
    //-EXPECT-ERROR: Fixed-size array
    //int bad_scalar[3] = 5;

    /* negative: inner-shape mismatch. outer arity 2 matches dims[1]=3
       (decl-form tail-short ok at outer); but the first inner slot has
       3 values when dims[0]=2 expects 2. */
    //-EXPECT-ERROR: Too many values for array level
    //int bad_inner[2][3] = ((1, 2, 3), (4, 5));

    /* negative: a non-tuple slot at a non-leaf level. dims[0]=2 expects
       a 2-tuple in each outer slot; the first slot is a bare int. */
    //-EXPECT-ERROR: Init slot must be a tuple
    //int bad_outer[2][3] = (1, (2, 3), (4, 5));

    /* negative: multi-dim slid arrays are not yet supported. */
    //-EXPECT-ERROR: Multi-dimensional fixed-size array of slid type
    //Cell board_2d[3][3];

    return 0;
}
