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

    /* unexpected compile error: */
    //array1[2] = (tuple, tuple);

    /* unexpected compile error: */
    (Class, Class) array2[2] = (tuple, tuple);

    TupleType array[2] = (tuple, tuple);
    x = array[0][0][0];
    __println("array[0][0][0]=" + x);

    /* multi-dim native array: row-major flat alloca, dims=[2,3].
       chain read via grid[i][j] folds both indices into one flat GEP. */
    int grid[2][3] = (10, 20, 30, 40, 50, 60);
    __println("grid[0][0]=" + grid[0][0]);
    __println("grid[1][2]=" + grid[1][2]);

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
