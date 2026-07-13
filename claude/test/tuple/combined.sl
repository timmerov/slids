/*
test combinations of arrays, tuples, and classes.

array of tuples.
tuple of arrays.

assignments are handled by slot/element iteratively and recursively.

arrays of tuples.

    (int, int) arr[3] = ((1,2), (3,4), (5,6));
    six = arr[2][1];
    six = arr[1,2];

mixed assignments:

    int[2] arr1 = (1,2);
    (int,int) tpl1 = arr1;
    arr1 = tpl1;
    tpl1 = arr1;
    arr1 <-- tpl1;
    tpl1 <-- arr1;

complex mixed assignments:

    (int[2], int[2]) tpl2 = ((1,2), (3,4));
    (int,int) arr2[2] = tpl2;
    tpl2 = arr2;
    arr2 = tpl2;
    tpl2 <-- arr2;
    arr2 <-- tpl2;
*/

/*
claude says:

arrays and tuples are ONE homogeneous-aggregate shape — an array is a tuple whose
slots are identical and runtime-indexable. They interoperate by slot/element,
iteratively and recursively, in both assignment and arithmetic.

covered here:
  - array of tuples / multi-dim array of tuples / tuple of arrays, read + store
    through the composed index chain; alias-typed array elements; const-expr dims
    in a tuple slot type (folded + baked, ##type reports the baked dims).
  - CROSS-FORM copy + move: array <-> tuple, assigned or moved (<--) in both
    directions, simple (int[2] <-> (int,int)) and nested ((int[2],int[2]) <->
    (int,int)[2]). The copy lowers BY SLOT — an array index and a tuple slot are
    the same i-th sub-component — recursing to scalar leaves; the two forms are
    one homogeneous aggregate. (Swap <--> stays exact-type, so it is not cross-form.)
  - MIXED arithmetic: array op tuple and tuple op array. Both run element-wise
    (matching shape required) and yield a TUPLE — the more general heterogeneous-
    capable shape — so ##type(array + tuple) is a tuple. op= mixes the same way:
    the lvalue's kind is the store target (array += tuple stores the tuple result
    back through the array<-tuple relation; tuple += array stores into the tuple),
    and the arithmetic itself is the shared slot-wise path (the SAME one the binary
    op uses — classify routes aug-assign through it so they can't diverge).

the slot-wise arithmetic recurses for a nested aggregate slot (array-of-tuples op
array-of-tuples) and broadcasts a scalar; a per-element narrow is rejected at
classify. array op array stays an array; tuple op tuple stays a tuple (those live
in array.sl / anon.sl).

negatives: a const-expr tuple-slot dim is fine, a runtime dim isn't; an array /
tuple VALUE element whose type / arity doesn't match the declared element.
*/

/* CROSS-FORM RETURN — the function's return type and the returned VALUE differ in
   form; the return seam converts by slot, form-agnostically (both directions). */
(int[2], int[2]) retTOA() {              // returns tuple-of-arrays
    (int, int) a[2] = ((1,2), (3,4));    // body builds an array-of-tuples value
    return a;
}
(int, int)[2] retAOT() {                 // returns array-of-tuples
    (int[2], int[2]) t = ((5,6), (7,8)); // body builds a tuple-of-arrays value
    return t;
}
(int, int) widenCross() {                // (int,int) returned from an int8[2] value
    int8 v[2] = (1, 2);                  // cross-form AND leaf-widen return
    return v;
}

// prints once per call — proves a side-effecting array index in a cross-form
// copy / move SOURCE is evaluated ONCE, not once per slot.
int pick() {
    __println("pick");
    return 1;
}

// A PLAIN-OLD-DATA class (fields, no ctor/dtor) used as an aggregate LEAF. Its
// layout is { i32, i32 }, so a class slot interoperates with a (int,int) slot by
// shape — the by-slot copy threads each WHOLE class value (default copy / move),
// exactly as a scalar leaf would.
Point(int x_, int y_) {
}

// CROSS-FORM RETURN with a class leaf — a tuple-of-class returned from an
// array-of-class body, converted by slot at the return seam.
(Point, Point) retPOA() {
    Point a[2] = ((1,2), (3,4));
    return a;
}

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

    /* MIXED array/tuple arithmetic — element-wise; a mixed result is a TUPLE
       (array op tuple AND tuple op array both yield a tuple, the more general
       heterogeneous-capable shape). */
    int32 ma[2] = (1, 2);
    (int32, int32) mt = (10, 20);
    (int32, int32) mr = ma + mt;                   // array op tuple -> tuple
    __println(##type(mr) + " mr= " + mr[0] + " " + mr[1]);   // (int32, int32) mr= 11 22
    (int32, int32) mr2 = mt + ma;                  // tuple op array -> tuple
    __println("mr2= " + mr2[0] + " " + mr2[1]);    // 11 22

    /* op= with mixed operands: the lvalue's kind is the store target; the
       arithmetic result is a tuple either way, stored back through the relation. */
    mt += ma;                                      // tuple lvalue += array
    __println("mt+=ma= " + mt[0] + " " + mt[1]);   // 11 22
    ma += (100, 200);                              // array lvalue += tuple
    __println("ma+=t= " + ma[0] + " " + ma[1]);    // 101 202

    /* an ALIAS-typed array operand mixes the same way (strip sees the array). */
    Vec2 va2 = (5, 6);
    (int, int) vr = va2 + (1, 1);                  // alias-array op tuple -> tuple
    __println("vr= " + vr[0] + " " + vr[1]);       // 6 7

    /* NESTED aggregate arithmetic — an array of tuples op= an array of tuples: the
       per-element op RECURSES into each tuple slot. */
    (int, int) np[2] = ((1,2), (3,4));
    (int, int) nq[2] = ((10,20), (30,40));
    np += nq;
    __println("np= " + np[0][0] + " " + np[0][1] + " "
              + np[1][0] + " " + np[1][1]);        // 11 22 33 44

    /* MIXED and NESTED unary — the operation by slot, recursively, whatever the forms.
       A unary had NO aggregate path at all (it emitted a numeric instruction on the whole
       struct: invalid IR), which is what a per-operator, hand-written aggregate walker
       buys you. There is one path now, so a unary needs no aggregate code of its own. */
    int32 ua[2] = (1, 2);
    (int32, int32) ut = -ua;                       // array operand, tuple result
    __println("-ua= " + ut[0] + " " + ut[1]);      // -1 -2
    (int, int) unp[2] = ((1,2), (3,4));            // array OF TUPLES
    (int, int) unr[2] = -unp;
    __println("-unp= " + unr[0][0] + " " + unr[1][1]);   // -1 -4

    /* SIMPLE cross-form copy — int[2] <-> (int,int). The copy is lowered BY SLOT
       (array index and tuple slot are the same i-th sub-component), in both
       directions and for both assign and move. */
    {
        int arr1[2] = (1, 2);
        (int, int) tpl1 = arr1;                  // tuple <- array value
        __println("tpl1= " + tpl1[0] + " " + tpl1[1]);              // 1 2
        tpl1 = (3, 4);
        arr1 = tpl1;                             // array <- tuple value
        __println("arr1= " + arr1[0] + " " + arr1[1]);              // 3 4

        (int, int) tm = (5, 6);
        int am[2] = (0, 0);
        am <-- tm;                              // array <-- tuple (move)
        __println("am<--tm= " + am[0] + " " + am[1]);               // 5 6
        int as[2] = (7, 8);
        (int, int) ts = (0, 0);
        ts <-- as;                              // tuple <-- array (move)
        __println("ts<--as= " + ts[0] + " " + ts[1]);               // 7 8
    }

    /* COMPLEX (nested) cross-form copy — (int[2],int[2]) <-> (int,int)[2]. The
       per-slot lowering RECURSES: each outer slot is itself a cross-form copy
       (tuple-slot int[2] <-> array-element (int,int)) down to scalar leaves. */
    {
        (int[2], int[2]) tpl2 = ((1,2), (3,4));
        (int, int) arr2[2] = tpl2;               // array-of-tuples <- tuple-of-arrays
        __println("arr2= " + arr2[0][0] + " " + arr2[0][1] + " "
                  + arr2[1][0] + " " + arr2[1][1]);                 // 1 2 3 4

        (int, int) src2[2] = ((10,20), (30,40));
        tpl2 = src2;                             // tuple-of-arrays <- array-of-tuples
        __println("tpl2= " + tpl2[0][0] + " " + tpl2[0][1] + " "
                  + tpl2[1][0] + " " + tpl2[1][1]);                 // 10 20 30 40

        (int[2], int[2]) mt2 = ((5,6), (7,8));
        (int, int) ma2[2] = ((0,0), (0,0));
        ma2 <-- mt2;                             // array-of-tuples <-- tuple-of-arrays
        __println("ma2= " + ma2[0][0] + " " + ma2[0][1] + " "
                  + ma2[1][0] + " " + ma2[1][1]);                   // 5 6 7 8
        (int, int) ms2[2] = ((9,10), (11,12));
        (int[2], int[2]) ts2 = ((0,0), (0,0));
        ts2 <-- ms2;                             // tuple-of-arrays <-- array-of-tuples
        __println("ts2= " + ts2[0][0] + " " + ts2[0][1] + " "
                  + ts2[1][0] + " " + ts2[1][1]);                   // 9 10 11 12
    }

    /* NESTED cross-form ARITHMETIC — array-of-tuples op tuple-of-arrays. The
       slot-wise op recurses across the differing forms; a mixed result is a
       TUPLE at every level. */
    {
        (int, int) xat[2] = ((1,2), (3,4));
        (int[2], int[2]) xta = ((10,20), (30,40));
        ((int,int), (int,int)) xsum = xat + xta;
        __println("xsum= " + xsum[0][0] + " " + xsum[0][1] + " "
                  + xsum[1][0] + " " + xsum[1][1]);                 // 11 22 33 44
    }

    /* CROSS-FORM move with POINTER leaves — array-of-pointers <-- tuple-of-pointers.
       The copy threads each pointer by slot; the move then nulls the SOURCE's
       pointer leaves (emitNullLeaves recurses the aggregate). */
    {
        int x = 3;
        int y = 4;
        int^ ap[2] = (^x, ^x);
        (int^, int^) tp = (^x, ^y);
        ap <-- tp;                              // array-of-ptrs <-- tuple-of-ptrs
        __println("ap= " + ap[0]^ + " " + ap[1]^);                 // 3 4
        __println("tpNull= " + !tp[0] + " " + !tp[1]);             // true true
    }

    /* CROSS-FORM RETURN — receive each function's cross-form result (the return
       seam converted the returned value into the declared return type by slot). */
    {
        (int[2], int[2]) c1 = retTOA();      // tuple-of-arrays <- array-of-tuples
        __println("c1= " + c1[0][0] + " " + c1[0][1] + " "
                  + c1[1][0] + " " + c1[1][1]);                    // 1 2 3 4
        (int, int) c2[2] = retAOT();         // array-of-tuples <- tuple-of-arrays
        __println("c2= " + c2[0][0] + " " + c2[0][1] + " "
                  + c2[1][0] + " " + c2[1][1]);                    // 5 6 7 8
    }

    /* CROSS-FORM SHIFT — the value and the count differ in form; the count matches
       by SHAPE (array==tuple) and the result keeps the LHS form. */
    {
        (int, int, int) tv = (1, 2, 3);
        int tcnt[3] = (3, 2, 1);
        (int, int, int) ts = tv << tcnt;     // tuple lhs, array count -> tuple
        __println("xshT= " + ts[0] + " " + ts[1] + " " + ts[2]);   // 8 8 6
        int av[3] = (1, 2, 3);
        int as[3] = av << (3, 2, 1);         // array lhs, tuple count -> array
        __println("xshA= " + as[0] + " " + as[1] + " " + as[2]);   // 8 8 6

        /* nested cross-form: array-of-tuples shifted by a tuple-of-arrays count. */
        (int, int) nat[2] = ((1,2), (3,4));
        (int[2], int[2]) nct = ((1,1), (2,2));
        (int, int) nr[2] = nat << nct;       // ((1<<1,2<<1),(3<<2,4<<2))
        __println("xshN= " + nr[0][0] + " " + nr[0][1] + " "
                  + nr[1][0] + " " + nr[1][1]);                    // 2 4 12 16
    }

    /* SWAP / MOVE a tuple slot that is itself an ARRAY — the mixed sub-aggregate
       case (a partial address into a tuple whose slot is an array). */
    {
        (int[2], int[2]) ta = ((1,2), (3,4));
        ta[0] <--> ta[1];                    // swap the two int[2] slots
        __println("slotAswap= " + ta[0][0] + " " + ta[0][1] + " "
                  + ta[1][0] + " " + ta[1][1]);                    // 3 4 1 2
        int rv[2] = (7, 8);
        ta[0] <-- rv;                        // move an array value into an array slot
        __println("slotAmove= " + ta[0][0] + " " + ta[0][1]);      // 7 8
    }

    /* CROSS-FORM + LEAF-WIDEN move/return — one array, one tuple, AND a widening
       leaf: the per-slot lowering bridges both the form and the leaf width at once. */
    {
        (int8, int8) xs = (5, 6);
        int xa[2] = (0, 0);
        xa <-- xs;                           // array <-- tuple, int8 -> int
        __println("xwmove= " + xa[0] + " " + xa[1]);               // 5 6
        (int, int) xr = widenCross();        // tuple <- int8[2] return
        __println("xwret= " + xr[0] + " " + xr[1]);                // 1 2
    }

    /* a side-effecting array index in a cross-form copy / move SOURCE is evaluated
       ONCE — "pick" prints once per statement. */
    {
        int cg[2][2] = ((1,2), (3,4));
        (int, int) ct = cg[pick()];          // cross-form copy: array row -> tuple
        __println("ct= " + ct[0] + " " + ct[1]);                   // 3 4
        (int, int) cm = (0, 0);
        cm <-- cg[pick()];                   // cross-form move: array row -> tuple
        __println("cm= " + cm[0] + " " + cm[1]);                   // 3 4
    }

    /* CLASS LEAVES — a POD class as the aggregate leaf. array-of-class and
       tuple-of-class interoperate by slot exactly like scalar leaves: each slot is
       a WHOLE class value, threaded by the same by-slot copy / move. */
    {
        (Point, Point) tp = ((1,2), (3,4));      // tuple of class
        Point pa[2] = ((5,6), (7,8));            // array of class
        __println("tp= " + tp[0].x_ + " " + tp[1].y_);             // 1 4
        __println("pa= " + pa[0].x_ + " " + pa[1].y_);             // 5 8

        /* CROSS-FORM copy both directions — array-of-class <-> tuple-of-class. */
        (Point, Point) ct = pa;                  // tuple-of-class <- array-of-class
        __println("ct= " + ct[0].x_ + " " + ct[1].y_);             // 5 8
        Point ca[2] = tp;                        // array-of-class <- tuple-of-class
        __println("ca= " + ca[0].x_ + " " + ca[1].y_);             // 1 4

        /* NESTED — a tuple whose slots are arrays-of-class; read through the
           composed slot/element/field chain. */
        (Point[2], Point[2]) toa = (((1,2),(3,4)), ((5,6),(7,8)));
        __println("toa= " + toa[0][0].x_ + " " + toa[1][1].y_);    // 1 8

        /* MOVE — default whole-aggregate move. POD has no pointer leaves, so the
           move is a value copy with no source null. */
        (Point, Point) md = ((0,0),(0,0));
        md <-- tp;
        __println("md= " + md[0].x_ + " " + md[1].y_);             // 1 4

        /* SWAP — a whole tuple-of-class, and a single class SLOT. */
        (Point, Point) sa = ((1,1),(2,2));
        (Point, Point) sb = ((3,3),(4,4));
        sa <--> sb;
        __println("sw= " + sa[0].x_ + " " + sb[1].y_);             // 3 2
        (Point, Point) ss = ((1,2),(3,4));
        ss[0] <--> ss[1];                        // swap two class slots
        __println("ssw= " + ss[0].x_ + " " + ss[1].y_);            // 3 2

        /* CROSS-FORM RETURN with a class leaf. */
        (Point, Point) cr = retPOA();            // tuple-of-class <- array-of-class
        __println("cr= " + cr[0].x_ + " " + cr[1].y_);             // 1 4

        /* CONSTRUCTION by slot — each class slot is CONSTRUCTED from its init value,
           iteratively and recursively, through the SAME path as `Point pt = 0`. A
           scalar is the slot-class's ctor input (x_ = scalar, y_ defaults to 0), so
           the slot's shape need NOT match the class layout — distinct from the
           matching-shape copies above. */
        (Point, Point) cs = (5, 9);              // each Point built from one scalar
        __println("cs= " + cs[0].x_ + " " + cs[0].y_ + " "
                  + cs[1].x_ + " " + cs[1].y_);                     // 5 0 9 0
    }

    /* test default initialization of an array/tuple of classes. */
    {
        Class(int x_ = 42) {
            _() { __println("Class:ctor: " + x_); }
            ~() { __println("Class:dtor: " + x_); }
        }

        {
            __println("expect ctors 42,42 after");
            Class arr[2];
            __println("expect ctors 42,42 before");
            __println(arr[0].x_ + " " + arr[1].x_);
            __println("expect dtors 42,42 after");
        }
        __println("expect dtors 42,42 before");

        {
            __println("expect ctors 42,42 after");
            (Class, Class) tuple;
            __println("expect ctors 42,42 before");
            __println(tuple[0].x_ + " " + tuple[1].x_);
            __println("expect dtors 42,42 after");
        }
        __println("expect dtors 42,42 before");

        /* the leaf class buried several layers deep in MIXED arrays and tuples:
           an ARRAY of a TUPLE holding an ARRAY-of-class and a class. A no-
           initializer decl default-constructs every leaf (x_ == 42), reachable
           as deep[i][0][j] (array->tuple->array->class) and deep[i][1]
           (array->tuple->class). ctors fire in declaration order, dtors mirror. */
        {
            __println("expect ctors 42 x6 after");
            ( Class[2], Class ) deep[2];
            __println("expect ctors 42 x6 before");
            __println("deep: " + deep[0][0][0].x_ + " " + deep[0][0][1].x_ + " " + deep[0][1].x_
                      + " | " + deep[1][0][0].x_ + " " + deep[1][0][1].x_ + " " + deep[1][1].x_);
            __println("expect dtors 42 x6 after");
        }
        __println("expect dtors 42 x6 before");

        /* same deep mixed shape, INITIALIZED from a tuple LITERAL — each class
           leaf is constructed from its slot value (1..6), proving the literal
           routes through every array/tuple layer to the buried class. */
        {
            __println("expect ctors 1..6 after");
            ( Class[2], Class ) lit[2] = ( ((1, 2), 3), ((4, 5), 6) );
            __println("expect ctors 1..6 before");
            __println("lit: " + lit[0][0][0].x_ + " " + lit[0][0][1].x_ + " " + lit[0][1].x_
                      + " | " + lit[1][0][0].x_ + " " + lit[1][0][1].x_ + " " + lit[1][1].x_);
            __println("expect dtors 6..1 after");
        }
        __println("expect dtors 6..1 before");

        /* same deep mixed shape, INITIALIZED from a tuple VARIABLE — a whole-value
           copy of `src`. A class can only be COPIED INTO, so the destination is
           CONSTRUCTED first and only then copied into: src's ctors run on its literal
           (1..6), and the copy's leaves are constructed with their FIELD DEFAULT (42 x6)
           before the copy overwrites them — so `cpy` reads back 1..6. Dtors mirror both. */
        {
            __println("expect ctors 1..6 (src) then 1..6 (copy) after");
            ( Class[2], Class ) src[2] = ( ((1, 2), 3), ((4, 5), 6) );
            ( Class[2], Class ) cpy[2] = src;
            __println("cpy: " + cpy[0][0][0].x_ + " " + cpy[0][0][1].x_ + " " + cpy[0][1].x_
                      + " | " + cpy[1][0][0].x_ + " " + cpy[1][0][1].x_ + " " + cpy[1][1].x_);
            __println("expect dtors (copy 6..1 then src 6..1) after");
        }
        __println("expect dtors before");
    }

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

/* a CROSS-FORM copy whose leaves would NARROW is rejected at the leaf (the shape
   check is form-agnostic and runs the per-leaf widen rule, so a narrowing leaf is
   caught at classify, not at codegen). */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int neg_crossform_narrow() {
//    (int[2], int[2]) s = ((1,2), (3,4));
//    (int8, int8) d[2] = s;
//    return d[0][0];
//}

/* a CROSS-FORM copy whose top-level slot COUNT differs (array-of-tuples has 2
   elements, the tuple has 3 slots) — rejected form-agnostically. */
//-EXPECT-ERROR: slot count differs
//int neg_crossform_count() {
//    (int, int) a[2] = ((1,2), (3,4));
//    (int, int, int) b = a;
//    return b[0];
//}

/* a CROSS-FORM shift count of mismatched shape (a 3-element array count, a 2-slot
   tuple lhs) — the count must match the lhs shape regardless of form. */
//-EXPECT-ERROR: Aggregate shapes differ
//int neg_crossform_shift() {
//    (int, int) t = (1, 2);
//    int cnt[3] = (1, 2, 3);
//    (int, int) r = t << cnt;
//    return r[0];
//}

/* a CROSS-FORM copy with CLASS leaves whose top-level slot COUNT differs (a
   tuple-of-class has 2 slots, the array has 3 elements) — rejected form-
   agnostically, the same as the scalar-leaf case above. */
//-EXPECT-ERROR: slot count differs
//int neg_class_crossform_count() {
//    (Point, Point) t = ((1,2), (3,4));
//    Point a[3] = t;
//    return a[0].x_;
//}

/* a class tuple SLOT whose initializer gives more values than the class has fields
   — validated per slot via the same constructClass path as a class ARRAY element
   and a direct class declaration. */
//-EXPECT-ERROR: Class 'Point' has 2 field(s) but 3 initializer(s) were given
//int neg_class_slot_arity() {
//    (Point, Point) t = ((1,2,3), (4,5));
//    return t[0].x_;
//}
