/*
test parsing of chained operations.

early work took some shortcuts.
and/or is incomplete.

claim lacks failure case:
"tuples of slids don't work."
tuple[0].field_;

this doesn't work when indexed thing changes:
array, tuple, op[]
thing[0][0][0];

there are probably lots more.
need an audit.
*/

Class(int x_ = 0) {
    int^ op[](int index) {
        return ^x_;
    }
}

Box(int v_ = 0) {
    int^ op[](int index) {
        return ^v_;
    }
}

/* caret in unhelpful location. */
//alsa TupleType = (Class, Class);
//               ^

alias TupleType = (Class, Class);

int32 main() {
    int x;
    Class cls1(1);
    Class cls2(2);

    tuple = (cls1, cls2);
    x = tuple[0].x_;
    __println("tuple[0].x_=" + x);
    x = tuple[1].x_;
    __println("tuple[1].x_=" + x);

    /* inferred-elem array decl: name[N] = ... with no leading type.
       elem type comes from init_values[0]; homogeneity is required. */
    array1[2] = (tuple, tuple);
    __println("array1[0][0].x_=" + array1[0][0].x_);

    (Class, Class) array2[2] = (tuple, tuple);

    TupleType array[2] = (tuple, tuple);
    x = array[0][0][0];
    __println("array[0][0][0]=" + x);

    /* multi-dim native array. Slids reading: leftmost type-bracket is
       innermost; rightmost is outermost. grid[2][3] is 3 outer of 2 inner.
       Init must structurally match: outer 3-tuple, each inner 2-tuple. */

    //-EXPECT-ERROR: Too many values for array level
    //int grid_flat[2][3] = (10, 20, 30, 40, 50, 60);

    int grid[2][3] = ((10, 11), (12, 13), (14, 15));
    __println("grid[0][0]=" + grid[0][0]);
    __println("grid[1][2]=" + grid[1][2]);

    /* Verify the access formula by writing through an iterator into the
       flat alloca and reading back via grid[i][j]. ^grid[0][0] is already
       typed int[] because the deepest base is a fixed-size array.
       Slids row-major: outer (j) slowest, inner (i) fastest. flat = j*2+i.
       Loop prints in (j, i) order so the output reads 0..5. */
    int[] iter = ^grid[0][0];
    for (k : 0..6) {
        iter[k] = k;
    }
    for (j : 0..3) {
        for (i : 0..2) {
            __println("grid[" + i + "][" + j + "]=" + grid[i][j]);
        }
    }

    /* op[] returns a reference; `box[i] = v` writes through it. */
    Box box;
    box[3] = 7;                      /* writes 7 into v_ via the returned ref */
    __println("box[0]=" + box[0]);   /* op[](0)^ reads v_ = 7 */
    __println("box[5]=" + box[5]);   /* op[](5)^ reads v_ = 7 */

    /* multi-dim native array write: indices fold into one flat GEP. */
    grid[0][0] = 100;
    grid[1][2] = 999;
    __println("grid[0][0]=" + grid[0][0]);
    __println("grid[1][2]=" + grid[1][2]);

    /* chain write ending in op[]^=: array[0] drills to Box,
       then op[](2)^ = 4 writes 4 into boxes[0].v_. */
    Box boxes[2];
    boxes[0][2] = 4;
    __println("boxes[0][0]=" + boxes[0][0]);   /* reads v_ = 4 */
    __println("boxes[0][7]=" + boxes[0][7]);   /* reads v_ = 4 */

    return 0;
}
