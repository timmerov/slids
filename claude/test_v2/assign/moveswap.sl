/*
test the move and swap operations.

move is a specialized assignment.
it means: copy and/or steal resources from the rhs.
the rhs must be left in a valid state.

move for primitives is copy - widening rules apply.
move for pointer types sets the rhs to nullptr - implicit casting rules apply.
move for classes calls the class's move operator.
for tuples and classes that do not have a move operator,
the move operator is applied by slot and field iteratively and recursively.

    int x <-- 1;
    int^ p <-- q;        // q is now nullptr
    Class c <-- object;  // c.op<--(^object);
    dst <-- tuple;
    arr[0] <-- arr[1];
    tuple[1] <-- tuple[0];
    c.field_ <-- 7;

swap exchanges the values.
lhs and rhs must be exactly the same type.
for tuples and classes, the swap operator is applied by
slot and field iteratively and recursively.
swap is the one assignment-like operation that does not
follow the assignment rules.

    x <--> y;
    tuple1 <--> tuple2;
    ptr <--> q;
    tuple[0] <--> tuple[1];
    arr[1] <--> arr[2];
    c.field1_ <--> c.field2_

swap is conceptually equivalent to:
though this might not be the exact implementaton.

    temp = x;
    x = y;
    y = temp;

fancy case:

    int x = 42;
    tuple1 = (1, (2, (3, ^x)));
    tuple2 <-- tuple1;

    tuple1 is now: (1, (2, (3, nullptr)))

arrays are homogeneous tuples.

    int x = 42;
    int y = 13;
    int z = 17;
    int^ src[3] = (^x, ^y, ^z);
    int^ dst[3] <-- src;

    src is now: [nullptr, nullptr, nullptr]

miscellaneous moves with pointers.

    ptr <-- nullptr;
    intptr num <-- ptr;  // ptr is nulled.

note:

lhs pointer types are clobbered.
which could be a memory leak.
*/

/*
claude says:

move is a specialized assignment: copy the rhs into the lhs (assignment rules —
widening for values, implicit pointer-cast for pointers), then null every
ADDRESSABLE pointer leaf of the rhs so the source is left valid. swap exchanges
two lvalues of EXACTLY the same type.

implemented in codegen (move + swap pass through desugar untouched):
  - move copy = emitExpr(rhs, dest = lhs type) + store — reuses the widen / tuple
    machinery; swap = SSA load/store exchange (no stack temp).
  - the null walks the rhs's structured type and GEPs into each pointer leaf,
    recursing through nested tuples (the fancy case).

covered here (classes deferred to Phase 5 — `op<--`/`op<-->`, class fields):
  - primitive move (copy, source unchanged), move from a literal rvalue, widening
    move; pointer (reference + iterator) move (source nulled); move statement.
  - pointer move with an implicit cast (`void^ v <-- int^q`); move from a
    non-lvalue pointer rvalue (`^y`) — pure copy, nothing to null.
  - swap of primitives, pointers, tuples, and a tuple holding a pointer (the
    pointers EXCHANGE, not nulled — the move/swap distinction).
  - tuple move with a pointer leaf (source slot nulled) + the nested fancy case.
  - whole-array move (plain + pointer arrays — pointer elements nulled) and
    whole-array swap (plain + pointer arrays — pointers EXCHANGE).
  - every lvalue operand form: bare var, array element, tuple slot, deref.
  - move-init declaration (`T x <-- y`) and move statement (`a <-- b`).

negatives: swap of mismatched types; move into a constant; swap of an
uninitialized operand; a non-lvalue swap operand; a narrowing move.
*/

int32 main() {

    /* ---- move: primitive is a copy (source unchanged) ---- */

    int a = 5;
    int b <-- a;                        // copy; a is untouched
    __println("b = " + b + " a = " + a);            // 5 5

    int c <-- 42;                       // move from a literal rvalue
    __println("c = " + c);                          // 42

    int64 w <-- a;                      // widening move (int -> int64)
    __println("w = " + w);                          // 5

    /* ---- move: pointer nulls the source ---- */

    int y = 9;
    int^ q = ^y;
    int^ p <-- q;                       // p takes q; q is nulled
    __println("p = " + p^ + " qnull = " + (q == nullptr));   // 9 true

    int^ r = ^y;
    int^ s = nullptr;
    s <-- r;                            // move statement; r nulled
    __println("s = " + s^ + " rnull = " + (r == nullptr));   // 9 true

    /* ---- swap exchanges values ---- */

    int x1 = 1;
    int x2 = 2;
    x1 <--> x2;
    __println("x1 = " + x1 + " x2 = " + x2);        // 2 1

    int m = 7;
    int n = 8;
    int^ pm = ^m;
    int^ pn = ^n;
    pm <--> pn;                         // pointers exchange (no nulling)
    __println("pm = " + pm^ + " pn = " + pn^);      // 8 7

    /* ---- tuple move: source pointer leaf nulled ---- */

    int z = 100;
    (int, int^) tsrc = (3, ^z);
    (int, int^) tdst <-- tsrc;          // copy; tsrc's pointer slot nulled
    __println("tdst1 = " + tdst[1]^ + " srcnull = " + (tsrc[1] == nullptr));   // 100 true

    /* ---- the fancy case: a deeply nested pointer leaf ---- */

    int deep = 42;
    (int, (int, (int, int^))) t1 = (1, (2, (3, ^deep)));
    (int, (int, (int, int^))) t2 <-- t1;
    __println("t2deep = " + t2[1][1][1]^ + " t1null = " + (t1[1][1][1] == nullptr));   // 42 true

    /* ---- swap tuples ---- */

    (int, int) ta = (10, 11);
    (int, int) tb = (20, 21);
    ta <--> tb;
    __println("ta0 = " + ta[0] + " tb0 = " + tb[0]);    // 20 10

    /* ---- indexed lvalues ---- */

    int arr[3];
    arr[0] = 0;
    arr[1] = 1;
    arr[2] = 2;
    arr[0] <-- arr[1];
    __println("arr0 = " + arr[0]);                  // 1
    arr[1] <--> arr[2];
    __println("arr1 = " + arr[1] + " arr2 = " + arr[2]);    // 2 1

    /* ---- iterator move (a pointer too — source nulled) ---- */

    int ia[3];
    ia[0] = 5;
    int[] it = ^ia[0];
    int[] jt <-- it;
    __println("jt = " + jt[0] + " itnull = " + (it == nullptr));    // 5 true

    /* ---- tuple-slot operands ---- */

    (int, int) pr = (3, 4);
    pr[1] <-- pr[0];                    // move one slot into another
    __println("pr1 = " + pr[1]);                    // 3
    (int, int) sw = (5, 6);
    sw[0] <--> sw[1];                   // swap two slots
    __println("sw0 = " + sw[0] + " sw1 = " + sw[1]);    // 6 5

    /* ---- deref-lvalue operands (swap the pointed-to values) ---- */

    int dy = 3;
    int dz = 9;
    int^ dp = ^dy;
    int^ dq = ^dz;
    dp^ <--> dq^;
    __println("dy = " + dy + " dz = " + dz);        // 9 3

    /* ---- pointer move with an implicit cast (int^ -> void^) ---- */

    int vy = 1;
    int^ vq = ^vy;
    void^ vv <-- vq;                    // implicit cast on the copy; vq nulled
    __println("vqnull = " + (vq == nullptr) + " vvset = " + (vv != nullptr));   // true true

    /* ---- swap a tuple holding a pointer: pointers EXCHANGE, not nulled ---- */

    int sa = 10;
    int sb = 20;
    (int, int^) g1 = (1, ^sa);
    (int, int^) g2 = (2, ^sb);
    g1 <--> g2;
    __println("g1p = " + g1[1]^ + " g2p = " + g2[1]^);    // 20 10

    /* ---- move from a non-lvalue pointer rvalue (addr-of): pure copy, no null ---- */

    int ay = 7;
    int^ ap <-- ^ay;
    __println("ap = " + ap^);                       // 7

    /* move an array of pointers. */
    int x3 = 42;
    int x4 = 37;
    int x5 = 99;
    int^ a1[3] = (^x3, ^x4, ^x5);
    int^ a2[3] <-- a1;
    __print("a1 = (");
    for (int^ ref : a1) {
        if (ref == nullptr) {
            __print(" nullptr");
        } else {
            __print(" " + ref^);
        }
    }
    __println(" )");
    __print("a2 = (");
    for (int^ ref : a2) {
        if (ref == nullptr) {
            __print(" nullptr");
        } else {
            __print(" " + ref^);
        }
    }
    __println(" )");

    /* whole-array swap (plain elements) — a whole-value exchange of the arrays. */
    int s1[3] = (1, 2, 3);
    int s2[3] = (4, 5, 6);
    s1 <--> s2;
    __println("s1 = " + s1[0] + " " + s1[1] + " " + s1[2]);   // 4 5 6
    __println("s2 = " + s2[0] + " " + s2[1] + " " + s2[2]);   // 1 2 3

    /* whole-array swap of a POINTER array — the pointers exchange (no nulling). */
    int^ b1[3] = (^x3, ^x4, ^x5);
    int^ b2[3] = (^x5, ^x4, ^x3);
    b1 <--> b2;
    __println("b1 = " + b1[0]^ + " " + b1[2]^);   // 99 42
    __println("b2 = " + b2[0]^ + " " + b2[2]^);   // 42 99

    /* multi-dim array: whole-value move/swap + the dim-product null walk. */
    int md1[2][2] = ((1, 2), (3, 4));
    int md2[2][2] <-- md1;
    __println("md2 = " + md2[0][0] + " " + md2[0][1] + " " + md2[1][0] + " " + md2[1][1]);   // 1 2 3 4
    int me1[2][2] = ((1, 2), (3, 4));
    int me2[2][2] = ((5, 6), (7, 8));
    me1 <--> me2;
    __println("me1 = " + me1[0][0] + " " + me1[1][1] + " me2 = " + me2[0][0] + " " + me2[1][1]);   // 5 8 / 1 4
    int^ mq1[2][2] = ((^x3, ^x4), (^x5, ^x3));
    int^ mq2[2][2] <-- mq1;
    __println("mq2 = " + mq2[0][0]^ + " mq1null = " + (mq1[0][0] == nullptr) + " " + (mq1[1][1] == nullptr));   // 42 / true true

    /* array of (int, int^): move nulls each element's pointer slot (array x tuple). */
    (int, int^) at1[2] = ((1, ^x3), (2, ^x4));
    (int, int^) at2[2] <-- at1;
    __println("at2 = " + at2[0][1]^ + " " + at2[1][1]^ + " at1null = " + (at1[0][1] == nullptr) + " " + (at1[1][1] == nullptr));   // 42 37 / true true

    /* iterator swap (iterators are pointers — they exchange, not null). */
    int isa[2];
    isa[0] = 11;
    isa[1] = 22;
    int[] i1 = ^isa[0];
    int[] i2 = ^isa[1];
    i1 <--> i2;
    __println("i1 = " + i1[0] + " i2 = " + i2[0]);   // 22 11

    /* float move + swap; move from a tuple-literal rvalue. */
    float32 fa <-- 1.5;
    float32 fb = 2.5;
    float32 fc <-- fb;
    __println("fa = " + fa + " fc = " + fc);   // 1.5 2.5
    float32 fx = 1.0;
    float32 fy = 2.0;
    fx <--> fy;
    __println("fx = " + fx + " fy = " + fy);   // 2 1
    (int, int) trv <-- (8, 9);
    __println("trv = " + trv[0] + " " + trv[1]);   // 8 9

    /* move from nullptr; move a pointer into intptr (source pointer nulled). */
    int^ pn0 <-- nullptr;
    __println("pn0null = " + (pn0 == nullptr));   // true
    int npv = 5;
    int^ npp = ^npv;
    intptr ipv <-- npp;
    __println("ipvset = " + (ipv != 0) + " nppnull = " + (npp == nullptr));   // true true

    /* compile errors — each uncommented in isolation by the negative runner. */

    /* swap requires exactly the same type — no widening. */
    //-EXPECT-ERROR: Swap operands must be the same type
    //int e1 = 1;
    //int64 e2 = 2;
    //e1 <--> e2;
    //__println("x= " + e1);

    /* a constant is not an assignable move target. */
    //-EXPECT-ERROR: Cannot assign to constant 'EK'
    //EK <-- 3;

    /* swap reads both operands, so each must already be initialized. */
    //-EXPECT-ERROR: Use of uninitialized variable 'eu'
    //int ei = 1;
    //int eu;
    //eu <--> ei;
    //__println("x= " + eu);

    /* both swap operands must be lvalues — a swap rhs is parsed as a general
       expression, so a non-lvalue is rejected (no storage to exchange) rather
       than crashing codegen. (A move's rhs, by contrast, may be an rvalue.) */
    //-EXPECT-ERROR: A swap operand must be an lvalue
    //int el = 1;
    //el <--> 7;
    //__println("x= " + el);

    /* move copies under widening rules, so a narrowing move is rejected. */
    //-EXPECT-ERROR: Cannot implicitly narrow 'int64' to 'int8'
    //int64 ew = 300;
    //int8 en <-- ew;
    //__println("x= " + en);

    /* a self-swap is a no-op and almost certainly a bug — rejected. */
    //-EXPECT-ERROR: Cannot swap a value with itself
    //int ss = 1;
    //ss <--> ss;
    //__println("x= " + ss);

    /* a self-move would null the source it just copied from — rejected. */
    //-EXPECT-ERROR: Cannot move a value onto itself
    //int^ smv = nullptr;
    //smv <-- smv;
    //__println("x= " + (smv == nullptr));

    return 0;
}

const int EK = 5;
