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
the order of the dimensions is standard.

    /* 5 chunks of 3 ints */
    int twodim[5][3];

    /* this accesses memory sequentially. */
    for (y : 0..5) {
        for (x : 0..3) {
            /* standard reading of a grid is y,x */
            twodim[y][x] = x*x + y;
        }
    }

the standard order is not a natural reading for accessing grids.
this is equivalent to the previous example.

    /* 3 columns, 5 rows */
    int twodim[3,5];

    /* this accesses memory sequentially. */
    for (y : 0..5) {
        for (x : 0..3) {
            /* natural reading of a grid is x,y */
            twodim[x,y] = x*x + y;
        }
    }

multiple comma separated array dimensions transpose order in a
declaration and when indexed.
this desugaring applies across index-able types - array, tuple, container classes.
it is type blind.

    [a,..,z] desugars to [z][...][a]


arrays can be set to a homogenous tuple.

    int arr[3] = (1,2,3);
    arr = (4,5,6);

    int twodim[2,3] = ((1,2), (3,4), (5,6));
    twodim = ((10,11), (12,13), (14,15));
    twodim[1] = (100,101);

a 2d array is an array of arrays.

    tuple = ((1,2), (3,4), (5,6));
    int twodim[2,3] = tuple;
    twodim[0] <--> twodim[2];

    twodim is now: ((5,6), (3,4), (1,2));

arrays of tuples.

    (int, int) arr[3] = ((1,2), (3,4), (5,6));
    six = arr[2][1];
    six = arr[1,2];
*/

/*
claude says:

- `T name[N]` declares a fixed-size array; the size N is a constant expression.
  the brackets follow the NAME (vs `T[]`, the iterator type suffix).
- `name[i]` subscripts -> an element lvalue (read or write); the index is a
  runtime integer. `^name[i]` is the element's address, an iterator (`T[]`).
- multi-dim is STANDARD row-major: `int twodim[5][3]` is 5 chunks of 3 ints
  (LLVM `[5 x [3 x i32]]`). `twodim[a][b]` -> a is the OUTER index (the row,
  0..5), b the INNER (the col, 0..3); flat = a*3 + b, so the LAST index varies
  fastest in memory. subscript[k] consumes dims[k] (positional); only the LLVM
  layout + GEP index order define the row-major-ness.
- the comma form is the "natural order" transpose: a comma list desugars
  `[a,..,z] -> [z]...[a]` purely in the grammar (type-blind), so `g[x,y]` ==
  `g[y][x]` and a declaration `int g[3,5]` == `int g[5][3]`.
- an array may be SET from a homogeneous tuple, at its declaration or by a
  whole-array assign: `int a[3] = (1,2,3)`, `a = (4,5,6)`. a multi-dim array
  takes a NESTED tuple whose SHAPE matches the declared dimensions (row × col):
  `int td[3][2] = ((1,2),(3,4),(5,6))` gives td[0][0]=1, td[0][1]=2, td[1][0]=3,
  ... classifyArrayFromTuple checks the leaf count AND the nesting shape (a
  transposed / flat literal is rejected, even with a matching leaf count). It
  lowers ELEMENT-WISE in row-major order (the tuple aggregate elided), each slot
  WIDENING into the element type.
  (Reach, in todo: sub-array assign `td[1] = (100,101)`, and arrays OF tuples
  `(int,int) a[3] = ((1,2),(3,4),(5,6))`.)
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

    /* two-dimensional, standard row-major: int[5][3] is 5 rows of 3, indexed
       twodim[row][col] (row 0..5, col 0..3). */
    int twodim[5][3];
    for (y : 0..5) {
        for (x : 0..3) {
            twodim[y][x] = x * x + y;
        }
    }
    __println("twodim[0][0]= " + twodim[0][0]);  // 0
    __println("twodim[0][2]= " + twodim[0][2]);  // 4
    __println("twodim[4][0]= " + twodim[4][0]);  // 4
    __println("twodim[4][2]= " + twodim[4][2]);  // 8
    __println("twodim[3][1]= " + twodim[3][1]);  // 4

    /* ^twodim[row][col] is an iterator to that element. */
    int[] it2 = ^twodim[3][1];
    __println("it2^= " + it2^);                  // 4

    /* the comma form transposes: grid[x,y] == grid[y][x], and a comma
       declaration int[3,5] == int[5][3]. */
    int grid[3,5];                               // == int[5][3]
    for (y : 0..5) {
        for (x : 0..3) {
            grid[x,y] = x * x + y;               // == grid[y][x]
        }
    }
    __println("grid[2,3]= " + grid[2,3]);        // x=2,y=3 -> 7
    __println("grid[3][2]= " + grid[3][2]);      // same element, chained -> 7
    __println("grideq= " + (grid[2,3] == grid[3][2]));   // true

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
    int cg[N][3];                                // int[4][3] (4 rows of 3)
    cg[3][2] = 5;
    __println("cg[3][2]= " + cg[3][2] + " size= " + sizeof(cg)); // 5, 48

    /* an array may be SET from a homogeneous tuple — element-wise, the tuple
       aggregate elided, each slot widening into the element type. */
    int t1[3] = (1, 2, 3);
    __println("t1= " + t1[0] + " " + t1[1] + " " + t1[2]);       // 1 2 3
    t1 = (4, 5, 6);                              // whole-array assign from a tuple
    __println("t1b= " + t1[0] + " " + t1[1] + " " + t1[2]);      // 4 5 6

    /* multi-dim from a nested tuple: the literal's shape (rows × cols) must match
       the declared dimensions; it lowers row-major. */
    int td[3][2] = ((1,2), (3,4), (5,6));
    __println("td[0][0]= " + td[0][0]);          // 1
    __println("td[0][1]= " + td[0][1]);          // 2
    __println("td[1][0]= " + td[1][0]);          // 3
    __println("td[2][1]= " + td[2][1]);          // 6
    td = ((10,11), (12,13), (14,15));            // multi-dim whole-array assign
    __println("td2[0][0]= " + td[0][0]);         // 10
    __println("td2[2][1]= " + td[2][1]);         // 15

    /* a comma-declared array from a tuple: int[2,3] == int[3][2] (3 rows of 2). */
    int ct[2,3] = ((1,2), (3,4), (5,6));
    __println("ct= " + ct[0][0] + " " + ct[2][1]);  // 1 6

    /* per-slot WIDENING: int8 / int leaves widen into the int64 element type. */
    int8 e0 = 5;
    int e1 = 9;
    int64 wv[2] = (e0, e1);
    __println("wv= " + wv[0] + " " + wv[1]);     // 5 9

    /* three-dimensional, standard row-major: a[i][j][k] = i,j,k flat row-major. */
    int a3[2][3][4];
    for (i : 0..2) {
        for (j : 0..3) {
            for (k : 0..4) {
                a3[i][j][k] = i * 100 + j * 10 + k;
            }
        }
    }
    __println("a3[0][0][0]= " + a3[0][0][0]);     // 0
    __println("a3[1][2][3]= " + a3[1][2][3]);     // 123
    __println("a3[0][1][2]= " + a3[0][1][2]);     // 12
    __println("a3comma= " + a3[3,2,1]);           // == a3[1][2][3] -> 123

    /* a 3-D array from a fully-nested tuple — shape (2 × 2 × 2). */
    int n3[2][2][2] = (((1,2), (3,4)), ((5,6), (7,8)));
    __println("n3= " + n3[0][0][0] + " " + n3[1][1][1] + " " + n3[1][0][1]);  // 1 8 6

    /* a comma subscript composes with addr-of: ^a[i,j] == ^a[j][i]. */
    int g2[2][3];
    g2[1][2] = 7;
    int[] cit = ^g2[2,1];                        // == ^g2[1][2]
    __println("commaiter= " + cit^);             // 7

    /* an iterator walks the flat row-major layout across element boundaries. */
    int seq[5][3];
    for (y : 0..5) {
        for (x : 0..3) {
            seq[y][x] = x * x + y;
        }
    }
    int[] it = ^seq[0][1];                        // x=1,y=0 -> 1
    __println("it= " + it^);                      // 1
    it = it + 1;                                  // -> seq[0][2] -> x=2,y=0 -> 4
    __println("itnext= " + it^);                  // 4
    it = it + 1;                                  // -> seq[1][0] -> x=0,y=1 -> 1
    __println("itwrap= " + it^);                  // 1

    /* assign an array from a tuple variable. */
    (int8,int8,int8,int8) t4 = (1, 2, 3, 4);
    int a4[4] = t4;
    __print("a4 = (");
    for (x : a4) {
        __print(" " + x);
    }
    __println(" )");

    /* array of aliased type and named constant size. */
    alias Integer = int;
    const intptr n5 = 5;
    Integer a5[n5] = (10,9,8,7,6);
    __print("a5 = (");
    for (x : a5) {
        __print(" " + x);
    }
    __println(" )");

    /* assign a sub array. */
    int a6[2,3] = ((1,2), (3,4), (5,6));
    tuple = (7,8);
    a6[0] = a6[2];
    a6[1] = tuple;
    a6[2] = (9,10);
    __print("a6 = (");
    for (y : 0..3) {
        __print(" (");
        for (x : 0..2) {
            __print(" " + a6[x,y]);
        }
        __print(" )");
    }
    __println(" )");

    /* array of tuples. */
    (int,int) a7[3] = ((11,12), (13,14), (15,16));
    __print("a7 = (");
    for (y : 0..3) {
        __print("(" + a7[y][0] + ","+ a7[1,y] + ")");
    }
    __println(")");

    /* a MULTI-DIM array of tuples: 2 rows of 3, each element a (int,int). */
    (int,int) m7[2][3] = ( ((1,2),(3,4),(5,6)), ((7,8),(9,10),(11,12)) );
    __println("m7= " + m7[0][0][0] + " " + m7[1][2][1] + " " + m7[0][2][0]); // 1 12 5

    /* an alias-of-array as the ELEMENT type: `Vec2 va[3]` is a nested array; the
       index walk descends into the element, no flattening (alias preserved). */
    alias Vec2 = int[2];
    Vec2 va[3] = ((1,2), (3,4), (5,6));
    __println("va= " + va[0][0] + " " + va[2][1] + " " + va[1][0]);          // 1 6 3

    return 0;
}

/* a constant index outside an array's bounds is a compile error. */
//-EXPECT-ERROR: Array index 5 is out of bounds
//int neg_oob_1d() {
//    int arr[5];
//    arr[5] = 0;
//    return arr[0];
//}

/* the first (row) dimension of `int twodim[3][5]` is 3 — index 3 is out of bounds */
//-EXPECT-ERROR: Array index 3 is out of bounds
//int neg_oob_2d_row() {
//    int twodim[3][5];
//    twodim[3][0] = 0;
//    return twodim[0][0];
//}

/* the second (col) dimension is 5 — index 5 is out of bounds */
//-EXPECT-ERROR: Array index 5 is out of bounds
//int neg_oob_2d_col() {
//    int twodim[3][5];
//    twodim[0][5] = 0;
//    return twodim[0][0];
//}

/* an initializer whose tuple SHAPE doesn't match the declared dimensions is an
   error even when the leaf count matches (a 3×2 literal into a 2×3 array). */
//-EXPECT-ERROR: Array initializer shape does not match
//int neg_shape_mismatch() {
//    int a[2][3] = ((1,2), (3,4), (5,6));
//    return a[0][0];
//}

/* a FLAT tuple into a multi-dim array is rejected too — the leaf count matches
   but the nesting doesn't (the top level isn't dims[0] tuples). */
//-EXPECT-ERROR: Array initializer shape does not match
//int neg_shape_flat() {
//    int a[2][3] = (1, 2, 3, 4, 5, 6);
//    return a[0][0];
//}

/* a char-constant index is bounds-checked too ('A' is 65). */
//-EXPECT-ERROR: Array index 65 is out of bounds
//int neg_oob_char() {
//    int arr[5];
//    arr[0] = 1;
//    return arr['A'];
//}

/* a partial index yields a SUB-ARRAY value; assigning it to a scalar is a type
   mismatch. */
//-EXPECT-ERROR: Cannot assign 'int[5]' to 'int'
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

/* OVER-indexing — one subscript past the rank — names the over-indexed array
   (the symmetric case of the partial-index error above). */
//-EXPECT-ERROR: Subscript indexes past the last dimension of 'int[3][5]'
//int neg_over_index() {
//    int twodim[3][5];
//    twodim[0][0] = 1;
//    return twodim[0][2][1];
//}

/* the comma form over-indexes the same way (a[i,j,k] on a 2-D array). */
//-EXPECT-ERROR: Subscript indexes past the last dimension of 'int[2][3]'
//int neg_over_index_comma() {
//    int a[2][3];
//    a[0][0] = 1;
//    return a[1,1,1];
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

/* a LITERAL dimension is validated the same way — a zero literal is rejected. */
//-EXPECT-ERROR: Array size must be a positive integer constant.
//int neg_dim_zero_literal() {
//    int a[0];
//    a[0] = 1;
//    return a[0];
//}

/* every dimension is validated, including a non-first one in a multi-dim array. */
//-EXPECT-ERROR: Array size must be a positive integer constant.
//int neg_dim_zero_multi() {
//    int a[3][0];
//    a[0][0] = 1;
//    return a[0][0];
//}

/* a zero in a comma dim list is rejected too (it desugars to a chained dim). */
//-EXPECT-ERROR: Array size must be a positive integer constant.
//int neg_dim_zero_comma() {
//    int a[0,3];
//    a[0][0] = 1;
//    return a[0][0];
//}

/* a const-expression dimension must be an integer (a float is rejected). */
//-EXPECT-ERROR: Array size must be an integer constant.
//int neg_dim_float() {
//    const float F = 2.0;
//    int a[F];
//    a[0] = 1;
//    return a[0];
//}

/* an array size in TYPE position (not on the name) is rejected in a declaration;
   write `int x[N]`. */
//-EXPECT-ERROR: An array size belongs on the declared name
//int neg_array_type_local() {
//    int[3] x = (1,2,3);
//    return x[0];
//}

/* the same rule for a const declaration. */
//-EXPECT-ERROR: An array size belongs on the declared name
//int neg_array_type_const() {
//    const int[3] c = (1,2,3);
//    return c[0];
//}
