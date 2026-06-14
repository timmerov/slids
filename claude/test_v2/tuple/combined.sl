/*
test combinations of arrays and tuples.

array of tuples.
tuple of arrays.

arrays of tuples.

    (int, int) arr[3] = ((1,2), (3,4), (5,6));
    six = arr[2][1];
    six = arr[1,2];
*/

/*
claude says:

tbd
*/

int32 main() {

    /* array of tuples. */
    (int,int) a7[3] = ((11,12), (13,14), (15,16));
    __print("a7 = (");
    for (y : 0..3) {
        __print("(" + a7[y][0] + ","+ a7[1,y] + ")");
    }
    __println(")");

    /* the same array initialized from element VALUES (not literals) — each whole
       element is loaded and stored. */
    alias Vec2 = int[2];
    Vec2 av = (7, 8);
    Vec2 vv[2] = (av, av);
    __println("vv= " + vv[0][0] + " " + vv[1][1]);                           // 7 8

    /* a MULTI-DIM array of tuples: 2 rows of 3, each element a (int,int). */
    (int,int) m7[2][3] = ( ((1,2),(3,4),(5,6)), ((7,8),(9,10),(11,12)) );
    __println("m7= " + m7[0][0][0] + " " + m7[1][2][1] + " " + m7[0][2][0]); // 1 12 5

    /* const-EXPRESSION dims in a tuple slot type (a named const + an arithmetic
       expr): folded in constfold and baked into each slot's array type — ##type
       reports the baked dims, and the slot indexing sees the real sizes. */
    const int kN = 3;
    (int[kN], int[kN + 1]) t5 = ((1,2,3), (4,5,6,7));
    __println(##type(t5) + " t5= " + t5[0][2] + " " + t5[1][1]);  // (int[3], int[4]) t5= 3 5

    return 0;
}

/* compile errors — each uncommented in isolation by the negative runner. */

/* a const-EXPRESSION dim in an array TYPE (a tuple slot) is accepted (folded +
   baked in constfold); a RUNTIME (non-const) dim is still rejected — it can't fold
   to a constant size. */
//-EXPECT-ERROR: Array size must be an integer constant
//int neg_array_type_runtime_dim() {
//    int n = 3;
//    (int[n], int) t = ((1,2,3), 4);
//    return t[1];
//}

/* an array-VALUE element whose type doesn't match the array's element type. */
//-EXPECT-ERROR: does not match the declared element type
//int neg_array_value_elem() {
//    alias Vec2 = int[2];
//    Vec2 a = (1,2);
//    int wrong[5] = (5,6,7,8,9);
//    Vec2 va[2] = (a, wrong);
//    return va[0][0];
//}

/* a tuple-VALUE element whose arity doesn't match the array's tuple element. */
//-EXPECT-ERROR: does not match the declared element type
//int neg_tuple_value_elem() {
//    (int,int,int) bad = (1,2,3);
//    (int,int) at[2] = (bad, bad);
//    return at[0][0];
//}
