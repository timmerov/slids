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
