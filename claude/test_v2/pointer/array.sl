/*
test fixed size arrays.

square brackets around a const-expression in a variable declaration indicates
the variable is a fixed size array.
square brackets around a const-expression after an array variable is the lvalue
of the object at that position.
a caret ^ before an indexed array variable is an iterator.

    int arr[5];
    for (i : 0..5) {
        arr[i] = i * i;
    }
    int[] iter = ^arr[3];

multi dimensional arrays are row major in memory.
however, the order of the dimensions is backwards from standard.
cause sometimes the standard is stupid.

    /* 5 chunks of 3 ints */
    int twodim[3][5];

    /* this accesses memory sequentially. */
    for (y : 0..5) {
        for (x : 0..3) {
            /* natural reading of a grid is x,y */
            twodim[x][y] = x*x + y;
        }
    }
*/

/*
claude says:

- `T name[N]` declares a fixed-size array; the size N is a constant expression.
  the brackets follow the NAME (vs `T[]`, the iterator type suffix).
- `name[i]` subscripts -> an element lvalue (read or write); the index is a
  runtime integer. `^name[i]` is the element's address, an iterator (`T[]`).
- multi-dim is reversed: `int twodim[3][5]` is 5 chunks of 3 ints (LLVM
  `[5 x [3 x i32]]`). `twodim[a][b]` -> a is the INNER index (0..3), b the OUTER
  (0..5); flat = b*3 + a, so the inner index varies fastest in memory.
- no initializer list (tuples out): elements are filled by assignment.
*/

int32 main() {
    /* one-dimensional: fill via a ranged for, read back by subscript. */
    int arr[5];
    for (i : 0..5) {
        arr[i] = i * i;
    }
    __println("arr[0]= " + arr[0]);              // 0
    __println("arr[3]= " + arr[3]);              // 9
    __println("arr[4]= " + arr[4]);              // 16

    /* write through a subscript lvalue. */
    arr[2] = 100;
    __println("arr[2]= " + arr[2]);              // 100

    /* ^arr[i] is an iterator to that element; deref reads it. */
    int[] iter = ^arr[3];
    __println("iter^= " + iter^);                // 9

    /* two-dimensional: twodim[x][y] -> x inner (0..3), y outer (0..5). */
    int twodim[3][5];
    for (y : 0..5) {
        for (x : 0..3) {
            twodim[x][y] = x * x + y;
        }
    }
    __println("twodim[0][0]= " + twodim[0][0]);  // 0
    __println("twodim[2][0]= " + twodim[2][0]);  // 4
    __println("twodim[0][4]= " + twodim[0][4]);  // 4
    __println("twodim[2][4]= " + twodim[2][4]);  // 8
    __println("twodim[1][3]= " + twodim[1][3]);  // 4

    /* ^twodim[x][y] is an iterator to that element. */
    int[] it2 = ^twodim[1][3];
    __println("it2^= " + it2^);                  // 4

    /* const-EXPRESSION dimensions: a const, sizeof, an arithmetic expression, and
       a const dim in a multi-dim array. They fold in constfold and bake into the
       array type (so sizeof of the array sees the real size). */
    const int N = 4;
    int ca[N];                                   // int[4]
    ca[3] = 7;
    __println("ca[3]= " + ca[3] + " size= " + sizeof(ca));      // 7, 16
    int sa[sizeof(int)];                         // int[4]
    sa[3] = 8;
    __println("sa size= " + sizeof(sa));                        // 16
    int ea[N + 1];                               // int[5]
    ea[4] = 9;
    __println("ea[4]= " + ea[4] + " size= " + sizeof(ea));      // 9, 20
    int cg[N][3];                                // int[4][3] (3 chunks of 4)
    cg[3][2] = 5;
    __println("cg[3][2]= " + cg[3][2] + " size= " + sizeof(cg)); // 5, 48

    return 0;
}

/* a constant index outside an array's bounds is a compile error. */
//-EXPECT-ERROR: Array index 5 is out of bounds
//int neg_oob_1d() {
//    int arr[5];
//    arr[5] = 0;
//    return arr[0];
//}

/* the inner dimension of `int twodim[3][5]` is 3 — index 3 is out of bounds */
//-EXPECT-ERROR: Array index 3 is out of bounds
//int neg_oob_2d_inner() {
//    int twodim[3][5];
//    twodim[3][0] = 0;
//    return twodim[0][0];
//}

/* the outer dimension is 5 — index 5 is out of bounds */
//-EXPECT-ERROR: Array index 5 is out of bounds
//int neg_oob_2d_outer() {
//    int twodim[3][5];
//    twodim[0][5] = 0;
//    return twodim[0][0];
//}

/* a char-constant index is bounds-checked too ('A' is 65). */
//-EXPECT-ERROR: Array index 65 is out of bounds
//int neg_oob_char() {
//    int arr[5];
//    arr[0] = 1;
//    return arr['A'];
//}

/* every dimension must be indexed — a partial index has no scalar value. */
//-EXPECT-ERROR: An array subscript must index every dimension
//int neg_partial_index() {
//    int twodim[3][5];
//    twodim[0][0] = 1;
//    int y = twodim[0];
//    return y;
//}

/* a non-array value cannot be subscripted. */
//-EXPECT-ERROR: Cannot subscript a non-array value
//int neg_subscript_scalar() {
//    int s = 5;
//    return s[0];
//}

/* an array index must be an integer. */
//-EXPECT-ERROR: An array index must be an integer
//int neg_float_index() {
//    int arr[5];
//    arr[0] = 1;
//    return arr[1.5];
//}

/* reading an array element before any write is use-before-initialization. */
//-EXPECT-ERROR: Use of uninitialized variable 'arr'.
//int neg_use_before_init() {
//    int arr[5];
//    return arr[0];
//}

/* a non-constant array size (a runtime variable) is an error. */
//-EXPECT-ERROR: Array size must be an integer constant.
//int neg_dim_runtime() {
//    int v = 5;
//    int a[v];
//    a[0] = 1;
//    return a[0];
//}

/* a const-expression dimension must be positive (0 / negative rejected). */
//-EXPECT-ERROR: Array size must be a positive integer constant.
//int neg_dim_zero() {
//    const int N = 0;
//    int a[N];
//    a[0] = 1;
//    return a[0];
//}

/* a const-expression dimension must be an integer (a float is rejected). */
//-EXPECT-ERROR: Array size must be an integer constant.
//int neg_dim_float() {
//    const float F = 2.0;
//    int a[F];
//    a[0] = 1;
//    return a[0];
//}
