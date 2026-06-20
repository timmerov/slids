/*
test anonymous tuples.

a tuple is a comma separated list of types or data.
types can be mixed.
the slots are not named.
tuples are assign-able.
slots can be accessed by constant index.
tuples can be used whenever variables are used.
tuples are initializers to classes and arrays.
tuples may be nested.

    (1, 2, 3)
    ("Hello", 4.5, 7)
    pair = (Dir:kN, false);
    dir = pair[0];
    good = pair[1];
    other = pair;
    Class c = (123, "Maple");
    Class c(456, "Main");
    array[3][2] = ((1,2), (3,4), (5,6));

    ##type(pair) is (Dir, bool)
    (Dir, bool) function( (Dir, bool)^ tuple );
    pair = function(^other);

    #x desugars to:
    (##file, ##line, ##type(x), ##name(x), ^x)

tuples may be destructured into variables.
slots may be empty.

    (dir, result) = pair;
    (dir, ) = pair;

tuples may be operands in math operations.
the operation is applied by slot iteratively and recursively.
the types stored at each slot must be compatible via widening rules.
the tuples must have the same shape.

    (1,2,3) + (4,5,6) is (5,7,9)

a math operation between a scalar and a tuple applies the math
operation between the scalar and every slot iteratively and recursively.

    (1,2,3) + 7 = (8,9,10)

for loop iterates over a homogeneous tuple.

    for (x : (1,3,4)) { }

notes:

tuples generally must be size 2 or more.
a tuple of size 1 is a scalar.
a scalar is a tuple of size 1.
is this a parenthesized expression?
or a tuple of size 1?
answer: it's both.
they're interchangeable.

    (expr)

arrays are homogenous tuples that can be accessed by a runtime index.
*/

/*
claude says:

landing 1 — MVP. the smallest tuple that stands on its own.

covered here:
  - construct a tuple literal: (Dir:kN, false)
  - a tuple-typed local: (Dir, bool) pair = ...
  - whole-tuple copy with value semantics: (Dir, bool) other = pair;
  - read a slot by CONSTANT index, heterogeneous: pair[0] is Dir, pair[1] is bool
  - ##type renders the structured tuple type: ##type(pair) is (Dir, bool)

the tuple type is a real structured type object, not a type-string. the
(Dir, bool) spelling is rendered only for ##type and diagnostics.

a slot index must be a compile-time constant — the result type depends on a
static index. a runtime index is rejected (that is array subscript, not a tuple
slot).

deferred to later landings:
  - slot write (pair[0] = ...), destructuring ((a,b)=pair, empty slot (a,))
  - slot-wise math + scalar broadcast, move/swap (<-- / <-->)
  - tuple params/returns + tuple references
  - array-init-via-tuple, for-tuple, #x, class init
*/

enum Dir ( kN, kE, kS, kW );

/* landing 4 — tuple params / returns + tuple references */

(int, int) addpair( (int, int)^ p ) {       // by-value tuple param + tuple return
    return (p^[0] + 1, p^[1] + 1);
}

int firstRef( (int, int)^ pr ) {           // by-reference tuple param
    return pr^[0];                          // deref the ref, read slot 0
}

int32 main() {

    (Dir, bool) pair = (Dir:kN, false);

    (Dir, bool) other = pair;          // whole-tuple copy

    Dir  dir  = pair[0];               // const-index slot read (Dir)
    bool good = pair[1];               // const-index slot read (bool)

    __println(##type(pair));           // (Dir, bool)
    __println(##type(other));          // (Dir, bool)
    __println(##type(dir));            // Dir
    __println(##type(good));           // bool
    __println(good);                   // false

    /* landing 2 — slot write (destructure tests moved to destructure.sl) */

    pair[0] = Dir:kS;                  // write a slot by const index
    __println("w0= " + pair[0]);       // 2

    /* landing 3 — slot-wise math + scalar broadcast */

    (int, int, int) a3 = (1, 2, 3);
    (int, int, int) b3 = (4, 5, 6);

    (int, int, int) s3 = a3 + b3;      // slot-wise add: (5, 7, 9)
    __println("s3= " + s3[0] + " " + s3[1] + " " + s3[2]);   // 5 7 9

    (int, int, int) c3 = a3 + 7;       // scalar broadcast: (8, 9, 10)
    __println("c3= " + c3[0] + " " + c3[1] + " " + c3[2]);   // 8 9 10

    (int, int, int) d3 = 100 - a3;     // scalar on the LEFT: (99, 98, 97)
    __println("d3= " + d3[0] + " " + d3[1] + " " + d3[2]);   // 99 98 97

    /* compound (op=) on a tuple — element-wise, the SAME path as the binary op. */
    (int, int, int) e3 = (10, 20, 30);
    e3 += (1, 2, 3);                   // (11, 22, 33)
    __println("e3+= " + e3[0] + " " + e3[1] + " " + e3[2]);   // 11 22 33
    e3 *= (2, 2, 2);                   // (22, 44, 66)
    __println("e3*= " + e3[0] + " " + e3[1] + " " + e3[2]);   // 22 44 66

    /* float-element slot-wise arithmetic (the float instr path per slot). */
    (float32, float32) f2 = (1.5, 2.5);
    f2 += (0.25, 0.25);                // (1.75, 2.75)
    __println("f2= " + f2[0] + " " + f2[1]);                  // 1.75 2.75

    /* bitwise, element-wise. */
    (int, int) bw = (12, 12) & (10, 6);
    __println("bw= " + bw[0] + " " + bw[1]);                  // 8 4

    /* landing 4 — tuple through functions */

    (int, int) q = (10, 20);
    (int, int) r = addpair(q);         // by-value param + tuple return
    __println("r= " + r[0] + " " + r[1]);   // 11 21

    __println("fr= " + firstRef(^q));  // by-reference param: 10

    /* assign a tuple from an array. */
    int8 a1[4] = (1,2,3,4);
    (int,int,int,int) t1 = a1;
    __println("t1=("+t1[0]+","+t1[1]+","+t1[2]+","+t1[3]+")");

    /* tuple of arrays. */
    (int[3], int[4]) t2 = ((1,2,3), (4,5,6,7));
    __print(##type(t2) + " t2 = ( (");
    for (i : 0..3) {
        __print(" " + t2[0][i]);
    }
    __print(" ) (");
    for (i : 0..4) {
        __print(" " + t2[i, 1]);
    }
    __println(")");

    /* a MULTI-DIM array slot (int[2][3]): the slot's own 2-D indexing composes
       with the tuple-slot read. */
    (int[2][3], int[4]) t3 = ( ((1,2,3),(4,5,6)), (7,8,9,10) );
    __println("t3= " + t3[0][0][0] + " " + t3[0][1][2] + " " + t3[1][3]);  // 1 6 10

    /* an ARRAY of tuples-of-arrays: a mixed array->tuple-slot->array index chain,
       handled by the per-segment index walk. */
    (int[3],int[4]) t4[2] = ( ((1,2,3),(4,5,6,7)), ((8,9,10),(11,12,13,14)) );
    __println("t4= " + t4[0][0][1] + " " + t4[1][1][3] + " " + t4[0][1][0]); // 2 14 4

    /* a STORE through the composed lvalue — the walk addresses array->tuple->array. */
    t4[0][0][1] = 99;
    __println("t4store= " + t4[0][0][1]);   // 99

    /* const-EXPRESSION dim composed with a pointer / iterator in the SAME chain.
       grammar buffers dim_exprs per RUN (a run is a maximal sequence of `[...]`
       chain steps with no intervening `^`/`[]`) and pushes runs into dim_sink
       in REVERSE order at chain end, so the sink push matches bakeDimsWalk's
       pre-order traversal. The canonical site is a tuple slot — the only place
       the chain can END in a sized dim (a named var/param requires the outer
       dim on the name). */
    const int kN = 2;
    int xa = 100;
    int xb = 200;
    (int, int^[kN]) t5 = (1, (^xa, ^xb));
    __println("t5= " + t5[0] + " " + t5[1][0]^ + " " + t5[1][1]^);    // 1 100 200

    /* an inner array with a const-dim behind a reference: `int[kN]^` is "ref to
       int[kN]". The kArray dim is INSIDE the kPointer; pre-order pops it AFTER
       descending the ref. Legal as a named var type — `^` is the outermost
       wrapper, not the sized dim. */
    int ka[kN] = (11, 22);
    int[kN]^ kp = ^ka;
    __println("kp= " + kp^[0] + " " + kp^[1]);                        // 11 22

    /* two const-dim array runs separated by a reference, in a tuple slot:
       `int[kN]^[kN]` — array of refs to array. The OUTER kArray (the LAST
       chain step) takes the LATER expression after the per-run reverse. */
    int ra[kN] = (33, 44);
    int rb[kN] = (55, 66);
    (int[kN]^[kN], int) tdouble = ((^ra, ^rb), 7);
    __println("tdouble= " + tdouble[0][0]^[0] + " " + tdouble[0][0]^[1]
              + " " + tdouble[0][1]^[0] + " " + tdouble[0][1]^[1]
              + " " + tdouble[1]);                                    // 33 44 55 66 7

    /* by-slot COPY within tuple form. A same-type whole-tuple copy is a single
       store; a LEAF-WIDEN copy ((int8,int8) -> (int,int)) is lowered BY SLOT into
       per-slot widening stores — at a declaration and at an assignment. */
    (int8, int8) p8 = (1, 2);
    (int, int) wp = p8;                    // tuple <- tuple value, per-slot widen
    __println("wp= " + wp[0] + " " + wp[1]);                         // 1 2
    (int8, int8) p8b = (3, 4);
    wp = p8b;                              // assign form
    __println("wpa= " + wp[0] + " " + wp[1]);                        // 3 4

    /* nested: a tuple-of-tuples leaf-widen copy recurses to each scalar leaf. */
    ((int8,int8), (int8,int8)) n8 = ((1,2), (3,4));
    ((int,int), (int,int)) wn = n8;
    __println("wn= " + wn[0][0] + " " + wn[0][1] + " "
              + wn[1][0] + " " + wn[1][1]);                          // 1 2 3 4

    /* MOVE and SWAP on tuples — same-type whole-aggregate ops. Move copies the
       source (nulling any pointer leaves); swap exchanges two same-type tuples. */
    (int, int) mvs = (7, 8);
    (int, int) mvd = (0, 0);
    mvd <-- mvs;                           // move
    __println("mvd<--= " + mvd[0] + " " + mvd[1]);                   // 7 8
    (int, int) swa = (1, 2);
    (int, int) swb = (3, 4);
    swa <--> swb;                          // swap
    __println("swap= " + swa[0] + " " + swa[1] + " "
              + swb[0] + " " + swb[1]);                              // 3 4 1 2

    /* a NESTED tuple with POINTER leaves — move copies the pointers and nulls each
       SOURCE leaf, recursing through the nested slots (emitNullLeaves). */
    int nx = 1;
    int ny = 2;
    int nz = 3;
    int nw = 4;
    ((int^,int^), (int^,int^)) npa = ((^nx,^ny), (^nz,^nw));
    ((int^,int^), (int^,int^)) npb = ((^nx,^nx), (^nx,^nx));
    npb <-- npa;
    __println("npmove= " + npb[0][0]^ + " " + npb[1][1]^);           // 1 4
    __println("npaNull= " + !npa[0][0] + " " + !npa[1][1]);          // true true

    /* SUB-TUPLE swap / move — a tuple SLOT is a swap/move operand (same as a
       sub-array row); the two slots must be the same type. */
    ((int,int), (int,int)) ts = ((1,2), (3,4));
    ts[0] <--> ts[1];                          // swap two same-type tuple slots
    __println("slotswap= " + ts[0][0] + " " + ts[1][0]);             // 3 1
    ((int,int), (int,int)) tm = ((0,0), (0,0));
    (int, int) sv = (9, 9);
    tm[1] <-- sv;                              // move a value into a tuple slot
    __println("slotmove= " + tm[1][0] + " " + tm[1][1]);             // 9 9

    {
        tuple = (1,2,3);
        tuple = tuple << 1;
        tuple <<= 1;
        __println(tuple[2]);
        tuple = (1,2,3) << (3,2,1);
        __println(tuple[0] + " " + tuple[1] + " " + tuple[2]);
    }

    /* shift: right-shift (signed ashr / unsigned lshr), nested, float (= multiply). */
    {
        (int, int) rs = (-8, 16);
        rs = rs >> 1;                          // signed: (-4, 8)
        __println("rs= " + rs[0] + " " + rs[1]);                     // -4 8
        (uint, uint) us = (16, 16);
        us >>= 2;                              // unsigned (lshr): (4, 4)
        __println("us= " + us[0] + " " + us[1]);                     // 4 4
        ((int,int),(int,int)) nsh = ((1,2),(3,4));
        nsh = nsh << 1;                        // nested, recurses: ((2,4),(6,8))
        __println("nsh= " + nsh[0][0] + " " + nsh[1][1]);            // 2 8
        (float32, float32) fsh = (1.5, 2.0);
        fsh = fsh << 1;                        // float << = multiply: (3.0, 4.0)
        __println("fsh= " + fsh[0] + " " + fsh[1]);                  // 3 4
        (float32, float32) fd = (4.0, 8.0);
        fd = fd >> 1;                          // float >> = divide: (2.0, 4.0)
        __println("fd= " + fd[0] + " " + fd[1]);                     // 2 4
        (int, int) mw = (1, 2);
        int8 cmw = 2;
        mw = mw << cmw;                        // mixed-width count (int8 into int)
        __println("mw= " + mw[0] + " " + mw[1]);                     // 4 8
        (int, int) ex = (1, 2);
        __println("ex= " + (ex << 2)[0]);      // shift result indexed directly: 4
    }

    return 0;
}

/* compile errors — each uncommented in isolation by the negative runner. */

/* comparison is not defined on a tuple (only arith / bitwise apply slot-wise). */
//-EXPECT-ERROR: Operator '==' is not defined on a tuple
//int neg_tuple_cmp() {
//    (int,int) a = (1,2);
//    bool z = a == (1,2);
//    return z;
//}

/* tuple operands of differing shape have no element-wise result. */
//-EXPECT-ERROR: Aggregate shapes differ
//int neg_tuple_shape() {
//    (int,int) a = (1,2);
//    (int,int,int) b = a + (1,2,3);
//    return b[0];
//}

/* a slot-wise shift count must match the lhs shape (a 2-tuple shifted by a 3-tuple). */
//-EXPECT-ERROR: A slot-wise shift needs a matching-shape count
//int neg_shift_shape() {
//    (int,int) a = (1,2);
//    (int,int) b = a << (1,2,3);
//    return b[0];
//}

/* a scalar shift count must be integer-class, not a float. */
//-EXPECT-ERROR: Shift count must be integer-class
//int neg_shift_float_count() {
//    (int,int) a = (1,2);
//    a = a << 1.5;
//    return a[0];
//}

/* a SCALAR value shifted by an AGGREGATE count is rejected — the count is not a
   single integer. */
//-EXPECT-ERROR: Shift count must be integer-class
//int neg_scalar_lhs_agg_count() {
//    (int,int) c = (1,2);
//    int x = 8 << c;
//    return x;
//}

/* a count that matches at the TOP level but mismatches a NESTED slot is rejected. */
//-EXPECT-ERROR: A slot-wise shift needs a matching-shape count
//int neg_shift_nested_shape() {
//    ((int,int),(int,int)) x = ((1,1),(1,1));
//    ((int,int),(int,int)) y = x << ((1,1),(1,1,1));
//    return y[0][0];
//}

/* a swap of two DIFFERENT-typed tuple slots is rejected — swap is exact-type. */
//-EXPECT-ERROR: Swap operands must be the same type
//int neg_swap_hetero_slot() {
//    (int, bool) h = (1, true);
//    h[0] <--> h[1];
//    return h[0];
//}
