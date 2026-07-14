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
- a SUB-ARRAY row is an lvalue: it can be assigned (`td[1] = (100,101)`), op-
  assigned, moved, and swapped. an array OF tuples is a nested aggregate too
  (`(int,int) a[3] = ((1,2),(3,4),(5,6))`).
*/

int[3] widenArr() {                          // int[3] returned from an int8[3] value
    int8 v[3] = (1, 2, 3);
    return v;                                 // leaf-widen return — lowered by slot
}

// prints once per call — proves a side-effecting array index in a by-slot copy /
// move SOURCE is evaluated ONCE (hoisted to a temp), not once per slot.
int pick() {
    __println("pick");
    return 1;
}

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

    /* an alias-of-array as the ELEMENT type: `Vec2 va[3]` is a nested array; the
       index walk descends into the element, no flattening (alias preserved). */
    alias Vec2 = int[2];
    Vec2 va[3] = ((1,2), (3,4), (5,6));
    __println("va= " + va[0][0] + " " + va[2][1] + " " + va[1][0]);          // 1 6 3

    /* reading a nested-array ELEMENT as a whole sub-array value. */
    int row[2] = va[0];
    __println("row= " + row[0] + " " + row[1]);                             // 1 2

    /* a REFERENCE to a SIZED array carries the size in the type (vs the unsized
       iterator `int[]`). A `(`-led grouped-const element type and an alias-led
       element type, each with a nested sized dim before the `^`, exercise the
       two declarator gates in var-decl position; `^base3` is the whole-array
       address (`int[3]^`). */
    int base3[3] = (1, 2, 3);
    (const int)[3]^ grp = ^base3;
    __println("grp= " + grp^[0] + " " + grp^[2]);                           // 1 3
    Integer[3]^ ali = ^base3;
    __println("ali= " + ali^[0] + " " + ali^[2]);                           // 1 3

    {
        // a size-1 array initialized from a bare SCALAR: size-1 tuples collapse to
        // their element, so the lone element's initializer is spelled bare. The
        // grouping-paren form `(2)` collapses to the same scalar.
        int arr[1] = 2;
        __println(arr[0]);                                                  // 2
        int brr[1] = (2);
        __println(brr[0]);                                                  // 2
        // a multi-dimensional unit array (one element total) takes the bare scalar
        // too — wrapped one 1-tuple per dim.
        int m11[1][1] = 7;
        __println(m11[0][0]);                                               // 7
    }

    /* array arithmetic — element-wise, an array is a homogeneous tuple. An
       array op array stays an ARRAY; op= applies the same path. */
    {
        int aa[3] = (1, 2, 3);
        int bb[3] = (10, 20, 30);
        int sum[3] = aa + bb;                 // array + array -> array: (11,22,33)
        __println("aa+bb= " + sum[0] + " " + sum[1] + " " + sum[2]);   // 11 22 33
        aa += bb;                             // array op= array
        __println("aa+=bb= " + aa[0] + " " + aa[1] + " " + aa[2]);     // 11 22 33

        // multi-dim: element-wise over the whole shape, and op= on a sub-array row.
        int mm[2][2] = ((1,2),(3,4));
        int nn[2][2] = ((10,20),(30,40));
        mm += nn;
        __println("mm+=nn= " + mm[0][0] + " " + mm[1][1]);            // 11 44
        mm[0] += (100, 200);                  // op= on a sub-array (array += tuple)
        __println("mm[0]+= " + mm[0][0] + " " + mm[0][1]);            // 111 222

        // float elements (the float instr path per element).
        float32 fa[2] = (1.5, 2.5);
        fa += (0.25, 0.25);
        __println("fa+= " + fa[0] + " " + fa[1]);                     // 1.75 2.75

        // bitwise, element-wise.
        int bw[2] = (12, 12);
        bw &= (10, 6);
        __println("bw&= " + bw[0] + " " + bw[1]);                     // 8 4

        // UNARY, element-wise — an operation on an array is the operation BY ELEMENT,
        // and a unary is an operation. It used to type the result as the array and then
        // emit a numeric instruction on the whole struct (invalid IR); now the array is
        // TAKEN APART and each element negates on its own.
        int ua[3] = (1, 2, 3);
        int un[3] = -ua;
        __println("-ua= " + un[0] + " " + un[1] + " " + un[2]);       // -1 -2 -3
        int um[2][2] = ((1,2),(3,4));
        int umn[2][2] = -um;                                          // multi-dim recurses
        __println("-um= " + umn[0][0] + " " + umn[1][1]);             // -1 -4
    }

    /* SCALAR distribution over an array — broadcast a scalar to every element (an
       array is a homogeneous tuple): binary, aug-assign, scalar-on-the-left,
       narrow-element flex, and multi-dim. */
    {
        int sb[3] = (1, 2, 3);
        int sr[3] = sb + 10;                  // array + scalar -> array: (11,12,13)
        __println("arr+s= " + sr[0] + " " + sr[1] + " " + sr[2]);     // 11 12 13
        sb += 10;                             // array += scalar
        __println("arr+=s= " + sb[0] + " " + sb[1] + " " + sb[2]);    // 11 12 13
        int sl[3] = (10, 20, 30);
        int sd[3] = 100 - sl;                 // scalar on the LEFT: (90,80,70)
        __println("s-arr= " + sd[0] + " " + sd[1] + " " + sd[2]);     // 90 80 70
        int8 n8[3] = (1, 2, 3);
        n8 += 1;                              // narrow element stays int8 (flex)
        __println("arr8+=s= " + n8[0] + " " + n8[1] + " " + n8[2]);   // 2 3 4
        int md[2][2] = ((1,2),(3,4));
        md += 10;                             // multi-dim broadcast: ((11,12),(13,14))
        __println("md+=s= " + md[0][0] + " " + md[1][1]);             // 11 14
    }

    /* by-slot COPY within array form. A same-type whole-array copy is a single
       store; a LEAF-WIDEN copy (int8[N] -> int[N]) is lowered BY SLOT into
       per-element widening stores — at a declaration and at an assignment. */
    {
        int8 s8v[3] = (1, 2, 3);
        int w3[3] = s8v;                      // array <- array value, per-elem widen
        __println("w3= " + w3[0] + " " + w3[1] + " " + w3[2]);        // 1 2 3
        int8 s8b[3] = (4, 5, 6);
        w3 = s8b;                             // assign form
        __println("w3a= " + w3[0] + " " + w3[1] + " " + w3[2]);       // 4 5 6

        // nested: a multi-dim leaf-widen copy recurses to each scalar leaf.
        int8 m8[2][2] = ((1,2), (3,4));
        int wm[2][2] = m8;
        __println("wm= " + wm[0][0] + " " + wm[0][1] + " "
                  + wm[1][0] + " " + wm[1][1]);                       // 1 2 3 4
    }

    /* MOVE and SWAP on arrays — same-type whole-aggregate ops. Move copies the
       source (nulling its pointer leaves, of which a value array has none); swap
       exchanges two same-type array lvalues. */
    {
        int mvs[3] = (7, 8, 9);
        int mvd[3] = (0, 0, 0);
        mvd <-- mvs;                          // move
        __println("mvd<--= " + mvd[0] + " " + mvd[1] + " " + mvd[2]); // 7 8 9

        int swa[3] = (1, 2, 3);
        int swb[3] = (4, 5, 6);
        swa <--> swb;                         // swap two arrays
        __println("swap= " + swa[0] + " " + swa[2] + " "
                  + swb[0] + " " + swb[2]);                           // 4 6 1 3

        // an array with POINTER leaves: move copies the pointers and NULLS the
        // source's pointer leaves (emitNullLeaves walks the array per element).
        int px = 5;
        int py = 6;
        int^ pa[2] = (^px, ^py);
        int^ pb[2] = (^px, ^px);
        pb <-- pa;
        __println("pmove= " + pb[0]^ + " " + pb[1]^);                // 5 6
        __println("paNull= " + !pa[0] + " " + !pa[1]);               // true true

        // SUB-ARRAY rows are swap/move operands too — a partial index addresses the
        // whole row (the swap/move uses allow_partial). swap exchanges two rows;
        // move overwrites one row from another array value.
        int g[3][2] = ((1,2), (3,4), (5,6));
        g[0] <--> g[2];                       // swap rows 0 and 2
        __println("rowswap= " + g[0][0] + " " + g[0][1] + " "
                  + g[2][0] + " " + g[2][1]);                        // 5 6 1 2
        int row[2] = (9, 9);
        g[1] <-- row;                         // move a value into a sub-array row
        __println("rowmove= " + g[1][0] + " " + g[1][1]);            // 9 9

        /* leaf-widen MOVE / RETURN — NOT-identical arrays (same form, differing
           leaf types). Lowered BY SLOT (per-element widening), like the leaf-widen
           COPY above. */
        int8 wsrc[3] = (5, 6, 7);
        int wdst[3] = (0, 0, 0);
        wdst <-- wsrc;                        // leaf-widen array move
        __println("awmove= " + wdst[0] + " " + wdst[1] + " " + wdst[2]); // 5 6 7
        int wret[3] = widenArr();             // leaf-widen array return
        __println("awret= " + wret[0] + " " + wret[1] + " " + wret[2]);  // 1 2 3
    }

    /* shift on arrays — element-wise (an array is a homogeneous tuple). A scalar
       count broadcasts; an array count applies per element; <<= / >>= mutate in
       place; multi-dim recurses. */
    {
        int sh[3] = (1, 2, 3);
        sh = sh << 1;                          // broadcast: (2,4,6)
        __println("sh<<= " + sh[0] + " " + sh[1] + " " + sh[2]);     // 2 4 6
        sh >>= 1;                              // (1,2,3)
        __println("sh>>= " + sh[0] + " " + sh[1] + " " + sh[2]);     // 1 2 3
        int pc[3] = (1, 2, 3);
        int ps[3] = pc << (3, 2, 1);           // per-element count: (8,8,6)
        __println("pshift= " + ps[0] + " " + ps[1] + " " + ps[2]);   // 8 8 6
        int aa[3] = (1, 2, 3);
        aa <<= (3, 2, 1);                      // aug-assign with a per-element count
        __println("aa<<= " + aa[0] + " " + aa[1] + " " + aa[2]);     // 8 8 6
        int msh[2][2] = ((1,2),(3,4));
        msh <<= 1;                             // multi-dim: ((2,4),(6,8))
        __println("msh= " + msh[0][0] + " " + msh[1][1]);            // 2 8
    }

    /* a side-effecting array index in a by-slot copy / move SOURCE is evaluated
       ONCE (hoisted to a temp), not once per slot — "pick" prints once per stmt. */
    {
        int8 seg[2][3] = ((1,2,3), (4,5,6));
        int sx[3] = seg[pick()];               // leaf-widen copy from seg[pick()]
        __println("sx= " + sx[0] + " " + sx[1] + " " + sx[2]);       // 4 5 6
        int sd[3] = (0, 0, 0);
        sd <-- seg[pick()];                    // leaf-widen move from seg[pick()]
        __println("sd= " + sd[0] + " " + sd[1] + " " + sd[2]);       // 4 5 6

        // codegen single-move paths: a SAME-type move and move-init from a side-
        // effecting index also evaluate the source ONCE.
        int seg2[2][3] = ((1,2,3), (4,5,6));
        int sm[3] = (0, 0, 0);
        sm <-- seg2[pick()];                   // same-type move
        __println("sm= " + sm[0] + " " + sm[1] + " " + sm[2]);       // 4 5 6
        int si[3] <-- seg2[pick()];            // same-type move-init
        __println("si= " + si[0] + " " + si[1] + " " + si[2]);       // 4 5 6
    }

    /* AN ARRAY STAYS AN ARRAY through the explode. The slots ride in a tuple literal, so an
       exploded array used to come back TYPED AS A TUPLE — which made every binding of it a
       CROSS-FORM copy (array <- tuple), lowered by spilling the whole aggregate to a temp and
       copying it leaf by leaf. Invisible here (a POD temp costs nothing observable) and
       expensive for classes (a ctor + a dtor per slot -- evaluate.sl Z2). These pin the
       re-formed TYPE: it must survive a MULTI-DIM shape, a WIDENED element (the element type
       is read off the SLOTS, not off the operand), and a LIVE target as well as a decl. */
    {
        int md[2][3] = ((1,2,3), (4,5,6));
        int me[2][3] = ((10,20,30), (40,50,60));
        int mf[2][3] = md + me;                // multi-dim: the dims fold back in
        __println("mf= " + mf[0][0] + " " + mf[1][2]);               // 11 66

        int8 wa[2] = (1, 2);
        int wb[2] = (10, 20);
        int wc[2] = wa + wb;                   // int8[2] + int[2] -> int[2] (WIDENED element)
        __println("wc= " + wc[0] + " " + wc[1]);                     // 11 22

        int la[2] = (1, 2);
        int lb[2] = (10, 20);
        int lc[2] = (0, 0);
        lc = la + lb;                          // a LIVE target, not a decl
        __println("lc= " + lc[0] + " " + lc[1]);                     // 11 22
    }

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

/* a slot-wise array shift count of the wrong length (3-element count, 2-element
   array) is rejected — the count must match the lhs shape, the same rule every
   aggregate operation asks. */
//-EXPECT-ERROR: Aggregate shapes differ
//int neg_array_shift_shape() {
//    int a[2] = (1,2);
//    int b[2] = a << (1,2,3);
//    return b[0];
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

/* the same rule for a `(`-led (grouped) type — the declarator gate now
   RECOGNIZES it as a declaration (top-level sized dim, no `^`), so it reaches
   the "size belongs on the name" diagnostic rather than a statement misparse. */
//-EXPECT-ERROR: An array size belongs on the declared name
//int neg_array_type_grouped() {
//    (const int)[3] x = (1,2,3);
//    return x[0];
//}

/* and for an alias-led (identifier) type. */
//-EXPECT-ERROR: An array size belongs on the declared name
//int neg_array_type_alias() {
//    alias E = int;
//    E[3] x = (1,2,3);
//    return x[0];
//}

/* a nested array-element initializer whose SHAPE doesn't match the element. */
//-EXPECT-ERROR: Array initializer shape does not match
//int neg_nested_elem_shape() {
//    alias Vec2 = int[2];
//    Vec2 va[2] = ((1,2,3), (4,5));
//    return va[0][0];
//}

/* comparison is not defined on an array (arith / bitwise apply element-wise). */
//-EXPECT-ERROR: Operator '==' is not defined on an array
//int neg_array_cmp() {
//    int a[2] = (1,2);
//    int b[2] = (1,2);
//    bool z = a == b;
//    return z;
//}

/* an aggregate op= whose result would NARROW back into the lvalue element. */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int neg_array_narrow() {
//    int8 a[2] = (1,2);
//    a += (1000, 2000);
//    return a[0];
//}

/* a bitwise op on a float element/slot. The aggregate is TAKEN APART, so the rejection
   comes from the ORDINARY scalar rule, naming the element's own type. */
//-EXPECT-ERROR: Bitwise '&' not defined on floating-point type 'float32'
//int neg_array_bitwise_float() {
//    float32 a[2] = (1.0, 2.0);
//    a &= (1.0, 1.0);
//    return 0;
//}
