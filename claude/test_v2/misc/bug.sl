/*
the bug of the day: an array initializer whose tuple SHAPE doesn't match the
declared dimensions must be a compile error. it was passing because the init
only checked the flattened leaf count, not the nesting. now standard row-major
arrays + the shape check reject a transposed literal.
*/

int32 main() {

    /* a 3x2 literal into a 3x2 array — shapes match, ok. */
    int arr32[3][2] = ((1,2), (3,4), (5,6));
    __println("arr32= " + arr32[0][0] + " " + arr32[2][1]);   // 1 6

    /* a 3x2 literal into a 2x3 array — same 6 leaves, wrong shape, now rejected. */
    //-EXPECT-ERROR: Array initializer shape does not match
    //int arr23[2][3] = ((1,2), (3,4), (5,6));
    //__println("x= " + arr23[0][0]);

    return 0;
}
