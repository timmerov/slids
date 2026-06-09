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

    x <--> y;
    tuple1 <--> tuple2;
    ptr <--> q;
    tuple[0] <--> tuple[1];
    arr[1] <--> arr[2];
    c.field1_ <--> c.field2_

desugars to:

    temp = x;
    x = y;
    y = temp;

fancy case:

    int x = 42;
    tuple1 = (1, (2, (3, ^x)));
    tuple2 <-- tuple1;

    tuple1 is now: (1, (2, (3, nullptr)))

note:

lhs pointer types are clobbered.
which could be a memory leak.

to be revisited:
can we apply move and swap to entire arrays?

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

covered here (classes deferred to Phase 5 — `op<--`/`op<-->`, class fields;
whole-array move/swap is the open "to be revisited" reach goal):
  - primitive move (copy, source unchanged), move from a literal rvalue, widening
    move; pointer (reference + iterator) move (source nulled); move statement.
  - pointer move with an implicit cast (`void^ v <-- int^q`); move from a
    non-lvalue pointer rvalue (`^y`) — pure copy, nothing to null.
  - swap of primitives, pointers, tuples, and a tuple holding a pointer (the
    pointers EXCHANGE, not nulled — the move/swap distinction).
  - tuple move with a pointer leaf (source slot nulled) + the nested fancy case.
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

    return 0;
}

const int EK = 5;
