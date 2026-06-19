/*
test combinations of arrays and tuples.

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
