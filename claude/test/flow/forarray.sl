/*
test for over a fixed size array.

to iterate by value, the array must be a primitive type.
all types may be iterated by reference.
loop variables are to be re-used if they exist in enclosing scope.
the loop variable type may be inferred.
in case of ambiguity, loop by value.

    int arr[5];

    /* iterate by reference */
    for (int^ ref : arr) {
        ref^ = 77;
    }

    /* iterate by value. */
    for (int x : arr) {
        println(String + "x = " + x);
    }

    /* iterate by value with inference. */
    for (x : arr) {
        println(String + "x = " + x);
    }

    /* iterate by reference with inference. */
    for (ref^ : arr) {
        ref^ = 77;
    }

the examples desugar to the following:

    for (
        intptr _$idx# = 0,
        int^ iter
    ) (
        _$idx# < arr.$size
    ) {
        ++_$idx#;
    } {
        iter = ^arr[_$idx#];
        /*body*/
    }

    for (
        intptr _$idx# = 0,
        int x
    ) (
        _$idx# < arr.$size
    ) {
        ++_$idx#;
    } {
        x = arr[_$idx#];
        /*body*/
    }

for the purposes of shadowing variables, there are 3 scopes counting the
enclosing scope:
normal local variable shadowing rules for scopes apply to these scopes.

    |--enclosing---------------|
    { for (var : array) {body} }
                        |body|
          |--loop-var--------|

note:
the compiler inserts instructions at the start of the loop body.
*/

/*
claude says:

- `for (v : arr) {body}` iterates a FIXED-SIZE array. Lowers to a kForLongStmt:
  a counter `_$idx < arr.$size` (the static size) stepped each pass, the loop var
  bound from `arr[_$idx]` as the body's FIRST instruction (compiler-inserted).
- by value (`int x`, or typeless `x`) requires a PRIMITIVE element. by reference
  (`int^ iter`) works for EVERY element type; a non-primitive element (a sub-array
  row, a class) FORCES a reference — there is no by-value copy of a non-primitive.
- a by-ref loop var aliases the element IN PLACE, so writes flow back (a by-ref
  loop can FILL an uninitialized array); a by-value loop var COPIES, so the array
  must already be initialized (else use-before-init).
- the loop var is typed or typeless (element type inferred). A typeless var REUSES
  an enclosing local of the same name if one exists — observable after the loop,
  and it may be wider than the element (int element into an int64 local) — else a
  fresh local. On ambiguity, by value.
- a 2-D array iterates ROWS: each row is a sub-array `int[N]` (non-primitive), so
  the loop var is a reference (`sub`); `sub^` then iterates the row. Class elements
  iterate by reference too (`for (ref : arr)` over `Class arr[3]`, `ref^.x_`).
- break / continue, a labeled break (`break scan` + `:scan`), and a numbered break
  (`break 2` exits N enclosing loops) all work in the body.
- a GLOBAL array iterates exactly like a local — the loop TOUCHES the lazy global
  (constructs it on the first element access) then reads/writes elements; by-value
  and by-reference both work, and a class-element global forces a reference. The
  resolve for-iterable dispatch accepts a kGlobalVar (not just kLocalVar), and a
  global is exempt from the by-value use-before-init check (always initialized).
  Exercised: `garr` (scalar, by value + by ref), `gboxes` (class element).
- INFER-AS-REFERENCE (`for (ref^ : arr)`): a typeless loop var with a trailing `^`
  binds each element's ADDRESS — `Elem^` fresh, and it REUSES an enclosing var only
  if that var IS `Elem^` or `(const Elem)^` (a compatible reuse reseats the binding;
  a primitive / iterator / const-dropping reference of the same name is a compile
  error, never a silent shadow). Works over 2-D rows (`sub2^`, and `v2^ : sub2^`
  through the array-EXPRESSION path), class elements (same as the forced bare
  reference), globals, and CONST arrays — typed or const-INFERRED — where the
  element's const rides into the reference ((const int)^, read-only). A range or
  an enum yields values, so `ref^ :` rejects there.
*/

import string;

alias Cell = int;

/* GLOBAL arrays — iterated like a local; the loop touches the lazy global on the
   first element access. A scalar array and a class-element array. */
global int garr[4] = (2, 4, 6, 8);
Box(int b_ = 0) { }
global Box gboxes[3];

/* a NAMESPACE-qualified global array. */
arrs {
    global int nsarr[3] = (3, 5, 7);
}

/* a class holding ARRAY FIELDS — iterated bare inside a method (by-ref fill +
   by-value read), via `self.`, via `obj.field` / a nested member chain from
   outside, and a 2-D array field (rows force a reference). A field array
   iterates IN PLACE (never a spilled copy), so writes land in the object. */
Grid(int data_[3], int cells_[2, 3]) {
    void fill() {
        int n = 1;
        for (r^ : data_) {
            r^ = n * 10;
            n = n + 1;
        }
    }
    int sumBare() {
        int t = 0;
        for (x : data_) {
            t = t + x;
        }
        return t;
    }
    int sumSelf() {
        int t = 0;
        for (x : self.data_) {
            t = t + x;
        }
        return t;
    }
    int sum2d() {
        int t = 0;
        for (row^ : cells_) {
            for (x : row^) {
                t = t + x;
            }
        }
        return t;
    }
}
Holder(Grid grid_) { }

/* sized-array PARAMS: a non-mutable param READS (its elements carry the munge's
   const promise — writes are negatives below); a `mutable` param's by-ref loop
   WRITES the caller's array. */
int sumParam(int a[4]) {
    int t = 0;
    for (x : a) {
        t = t + x;
    }
    return t;
}
void scaleParam(mutable int a[4]) {
    for (r^ : a) {
        r^ = r^ * 2;
    }
}

/* an array-returning call — the rvalue is SPILLED, then iterated. */
int[3] makeArr() {
    int a[3] = (7, 8, 9);
    return a;
}

int32 main() {
    int arr[5];

    /* by reference: fill the array through element references. */
    int n = 0;
    for (int^ iter : arr) {
        iter^ = n * n;
        n = n + 1;
    }
    println(String + "arr[0]= " + arr[0]);        // 0
    println(String + "arr[3]= " + arr[3]);        // 9

    /* by value, typed loop variable. */
    int sum = 0;
    for (int x : arr) {
        sum = sum + x;
    }
    println(String + "sum= " + sum);              // 30

    /* by value, typeless loop variable (inferred element type, fresh local). */
    for (y : arr) {
        println(String + "y= " + y);              // 0, 1, 4, 9, 16
    }

    /* typeless loop variable reuses an enclosing local — observable after. */
    int last = 0;
    for (last : arr) {
    }
    println(String + "last= " + last);            // 16

    /* break / continue work in a for-array body. */
    int found = -1;
    for (int x : arr) {
        if (x == 4) {
            found = x;
            break;
        }
    }
    println(String + "found= " + found);          // 4

    /* a labeled for-array, broken by name. */
    int count = 0;
    for (int x : arr) {
        count = count + 1;
        if (x == 4) {
            break scan;
        }
    } :scan;
    println(String + "count= " + count);          // 3

    /* continue skips an element. */
    int csum = 0;
    for (int x : arr) {
        if (x == 4) {
            continue;
        }
        csum = csum + x;
    }
    println(String + "csum= " + csum);            // 0+1+9+16 = 26

    /* a naked continue. */
    int ncsum = 0;
    for (int x : arr) {
        if (x == 0) {
            continue;
        }
        ncsum = ncsum + x;
    }
    println(String + "ncsum= " + ncsum);          // 1+4+9+16 = 30

    /* by reference, read only (no write-back). */
    int rsum = 0;
    for (int^ p : arr) {
        rsum = rsum + p^;
    }
    println(String + "rsum= " + rsum);            // 30

    /* non-int element types, by value: int64, char, float. */
    int64 big[3] = (100, 200, 300);
    int64 bsum = 0;
    for (int64 v : big) {
        bsum = bsum + v;
    }
    println(String + "bsum= " + bsum);            // 600

    char letters[3] = ('a', 'b', 'c');
    for (char c : letters) {
        print(String + "" + c);
    }
    println(String + "");                         // abc

    float fs[3] = (1.5, 2.5, 3.0);
    float fsum = 0.0;
    for (float f : fs) {
        fsum = fsum + f;
    }
    println(String + "fsum= " + fsum);            // 7

    /* an alias element type, iterated by reference. */
    Cell cells[3] = (5, 6, 7);
    int asum = 0;
    for (Cell^ p : cells) {
        asum = asum + p^;
    }
    println(String + "asum= " + asum);            // 18

    /* nested for-array. */
    int a2[2] = (10, 20);
    int b2[3] = (1, 2, 3);
    int xsum = 0;
    for (x : a2) {
        for (y : b2) {
            xsum = xsum + x + y;
        }
    }
    println(String + "xsum= " + xsum);            // 36 + 66 = 102

    /* a numbered break exits N enclosing for-arrays at once. */
    int firstpair = -1;
    for (x : a2) {
        for (y : b2) {
            firstpair = x + y;
            break 2;
        }
    }
    println(String + "firstpair= " + firstpair);  // 10 + 1 = 11

    /* a typeless loop variable reuses a WIDER enclosing local (int -> int64). */
    int64 wlast = 0;
    for (wlast : arr) {
    }
    println(String + "wlast= " + wlast);          // 16

    /* a single-element array. */
    int one[1];
    one[0] = 42;
    int osum = 0;
    for (v : one) {
        osum = osum + v;
    }
    println(String + "osum= " + osum);            // 42

    /* nested for over 2d array. */
    int a3[2,3] = ((1,2), (3,4), (5,6));
    print(String + "a3=(");
    for (sub : a3) {
        print(String + " (");
        for (x : sub^) {
            print(String + " " + x);
        }
        print(String + " )");
    }
    println(String + " )");

    {
        Class(int x_) { }
        Class arr[3] = (1,2,3);
        print(String + "arr=[");
        for (ref : arr) {
            print(String + " " + ref^.x_);
        }
        println(String + " ]");
        /* the same forced reference spelled explicitly (`ref^ :`). */
        print(String + "arr2=[");
        for (ref2^ : arr) {
            print(String + " " + ref2^.x_);
        }
        println(String + " ]");
    }

    /* GLOBAL array iteration — the loop TOUCHES the lazy global (constructs it on
       the first element access), by value and by reference. */
    int gsum = 0;
    for (int v : garr) { gsum = gsum + v; }
    println(String + "gsum= " + gsum);            // 20 (2+4+6+8)
    for (int^ p : garr) { p^ = p^ + 1; }
    println(String + "garr= " + garr[0] + " " + garr[3]);   // 3 9

    /* a GLOBAL CLASS array — a non-primitive element forces a reference. */
    print(String + "gboxes=[");
    for (ref : gboxes) {
        print(String + " " + ref^.b_);
    }
    println(String + " ]");                       // 0 0 0

    /* iterate by reference with INFERENCE (`ref^ :`) — fills like `int^ iter`. */
    int iarr[5];
    int m = 0;
    for (iref^ : iarr) {
        iref^ = m * 3;
        m = m + 1;
    }
    println(String + "iarr[4]= " + iarr[4]);      // 12

    /* `ref^ :` REUSES an enclosing var only if it IS a compatible reference —
       observable after the loop: it holds the LAST element's address. */
    int z0 = 0;
    int^ rlast = ^z0;
    for (rlast^ : iarr) {
    }
    println(String + "rlast^= " + rlast^);        // 12

    /* a `(const Elem)^` enclosing var is a compatible reuse — the loop reseats
       the binding; the const pointee makes the loop read-only. */
    (const int)^ crd = ^z0;
    int crsum = 0;
    for (crd^ : iarr) {
        crsum = crsum + crd^;
    }
    println(String + "crsum= " + crsum);          // 0+3+6+9+12 = 30

    /* 2-D rows spelled with infer-as-reference (the forced reference, made
       explicit); the inner loop derefs an array EXPRESSION (`sub2^`). */
    int rsum2 = 0;
    for (sub2^ : a3) {
        for (v2^ : sub2^) {
            rsum2 = rsum2 + v2^;
        }
    }
    println(String + "rsum2= " + rsum2);          // 21

    /* a CONST array iterates by inferred reference — the element's const rides
       into the reference (`(const int)^`); reading is fine (a write is a
       negative below). */
    const int cfix[3] = (10, 20, 30);
    int csum2 = 0;
    for (cr^ : cfix) {
        csum2 = csum2 + cr^;
    }
    println(String + "csum2= " + csum2);          // 60

    /* a TYPELESS const array (runtime const INFERENCE) iterates the same way. */
    int seed[3] = (4, 5, 6);
    const cinf = seed;
    int csum3 = 0;
    for (cr2^ : cinf) {
        csum3 = csum3 + cr2^;
    }
    println(String + "csum3= " + csum3);          // 15

    /* a GLOBAL array by inferred reference. */
    for (gr^ : garr) { gr^ = gr^ + 1; }
    println(String + "garr2= " + garr[0] + " " + garr[3]);   // 4 10

    /* array FIELDS: bare (fill + read inside methods), self., obj.field, a
       by-ref write from outside (in place), a 2-D field, a nested chain. */
    Grid g;
    g.fill();
    println(String + "gbare= " + g.sumBare());        // 60 (10+20+30)
    println(String + "gself= " + g.sumSelf());        // 60
    int gosum = 0;
    for (x : g.data_) { gosum = gosum + x; }
    println(String + "gobj= " + gosum);               // 60
    for (r^ : g.data_) { r^ = r^ + 1; }
    println(String + "gbumped= " + g.sumBare());      // 63
    println(String + "g2d= " + g.sum2d());            // 0 (default elements)
    Holder hg;
    hg.grid_.fill();
    int hsum = 0;
    for (x : hg.grid_.data_) { hsum = hsum + x; }
    println(String + "gnested= " + hsum);             // 60

    /* sized-array params: non-mutable read; mutable write flows to the caller. */
    int parr[4] = (1, 2, 3, 4);
    println(String + "psum= " + sumParam(parr));      // 10
    scaleParam(parr);
    println(String + "pscaled= " + parr[0] + " " + parr[3]);   // 2 8

    /* a namespace-qualified global array. */
    int nssum = 0;
    for (x : arrs:nsarr) { nssum = nssum + x; }
    println(String + "nssum= " + nssum);              // 15

    /* an array in a TUPLE SLOT, a whole-array reference deref, and an
       array-returning call (spilled). */
    (int[3], int) pslot = ((1, 2, 3), 9);
    int ssum = 0;
    for (x : pslot[0]) { ssum = ssum + x; }
    println(String + "slotsum= " + ssum);             // 6
    int warr[3] = (4, 5, 6);
    wref = ^warr;
    int wsum = 0;
    for (x : wref^) { wsum = wsum + x; }
    println(String + "wsum= " + wsum);                // 15
    int mcs = 0;
    for (x : makeArr()) { mcs = mcs + x; }
    println(String + "callsum= " + mcs);              // 24

    return 0;
}

/* a 2-D array iterates rows (each a sub-array `int[5]`); a by-ref loop var must
   reference the element, so `int^` (a reference to an int) is the wrong type. */
//-EXPECT-ERROR: Loop variable type 'int^' does not match the array element type 'int[5]'
//void neg_2d() {
//    int grid[3][5];
//    for (int^ it : grid) {
//        it^ = 0;
//    }
//}

/* a by-reference loop variable's base type must match the element type. */
//-EXPECT-ERROR: does not match the array element type
//void neg_type() {
//    int arr[5];
//    for (int64^ it : arr) {
//        it^ = 0;
//    }
//}

/* a non-primitive (sub-array) element forces a reference loop variable; a declared
   by-value loop var over a 2-D array's rows is rejected. */
//-EXPECT-ERROR: with non-primitive elements must use a reference
//void neg_byval_subarray() {
//    int matrix[2][3];
//    for (int sub : matrix) {
//    }
//}

/* an array EXPRESSION iterable (a sub-array slice) is named as an ARRAY — not a
   tuple — in a by-ref element mismatch. */
//-EXPECT-ERROR: does not match the array element type
//void neg_expr_byref() {
//    int matrix[2][3] = ((1,2,3),(4,5,6));
//    for (char^ p : matrix[0]) {
//        p^ = ' ';
//    }
//}

/* the right-hand side must be an array (or enum / tuple), not a scalar. */
//-EXPECT-ERROR: is not an array, enum, class, or tuple
//void neg_scalar() {
//    int v = 5;
//    for (int^ it : v) {
//        it^ = 0;
//    }
//}

/* a by-value loop variable too narrow for the element type is rejected (the
   error carets the loop variable). */
//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int neg_value_width() {
//    int arr[5];
//    for (i : 0..5) {
//        arr[i] = i;
//    }
//    for (int8 x : arr) {
//        println(String + "" + x);
//    }
//    return 0;
//}

/* reading an array BY VALUE before any write is use-before-init — a by-reference
   loop FILLS the array (no init required), a by-value loop READS it (init required). */
//-EXPECT-ERROR: Use of uninitialized variable 'arr'
//void neg_value_uninit() {
//    int arr[5];
//    for (int x : arr) {
//        println(String + "" + x);
//    }
//}

/* a loop variable that is bound each iteration but never read is an unused local. */
//-EXPECT-ERROR: set but never used
//int neg_unused_var() {
//    int arr[5];
//    for (i : 0..5) {
//        arr[i] = i;
//    }
//    for (int x : arr) {
//    }
//    return arr[0];
//}

/* `ref^ :` must reuse a REFERENCE — a same-name PRIMITIVE is a compile error,
   never a silent shadow or a store. */
//-EXPECT-ERROR: must reuse a reference to the element type
//void neg_ref_reuse_primitive() {
//    int arr[3];
//    int ref = 0;
//    for (ref^ : arr) {
//        ref^ = 1;
//    }
//    println(String + "ref= " + ref);
//}

/* ...and a same-name ITERATOR rejects too (a reference is not an iterator). */
//-EXPECT-ERROR: must reuse a reference to the element type
//void neg_ref_reuse_iterator() {
//    int arr[3];
//    arr[0] = 1;
//    int[] it = <int[]> <mutable> ^arr[0];
//    for (it^ : arr) {
//        it^ = 9;
//    }
//    println(String + "it^= " + it^);
//}

/* reusing a PLAIN reference over CONST elements would drop the const. */
//-EXPECT-ERROR: must reuse a reference to the element type
//void neg_ref_reuse_const_drop() {
//    const int carr[3] = (1,2,3);
//    int z = 0;
//    int^ p = ^z;
//    for (p^ : carr) {
//    }
//    println(String + "p^= " + p^);
//}

/* writing through an inferred reference to CONST elements. */
//-EXPECT-ERROR: Cannot write to a const value
//void neg_ref_const_write() {
//    const int carr[3] = (1,2,3);
//    for (r^ : carr) {
//        r^ = 9;
//    }
//}

/* a range yields values — there is no element to reference. */
//-EXPECT-ERROR: yields values; the for-loop variable cannot be a reference
//void neg_ref_range() {
//    for (r^ : 0..5) {
//        println(String + "" + r^);
//    }
//}

/* an enum yields member values — there is no storage to reference. */
//-EXPECT-ERROR: yields values; the for-loop variable cannot be a reference
//enum Dir ( kUp, kDown );
//void neg_ref_enum() {
//    for (r^ : Dir) {
//        println(String + "" + r^);
//    }
//}

/* writing through an inferred reference over a NON-MUTABLE sized-array param —
   the munge's element const applies inside the loop too. */
//-EXPECT-ERROR: Cannot write to a const value
//void neg_param_const_write(int a[3]) {
//    for (r^ : a) {
//        r^ = 9;
//    }
//}

/* a declared plain `int^` over a non-mutable param's (const) elements would
   drop the promise. */
//-EXPECT-ERROR: does not match the array element type 'const int'
//void neg_param_ref_drop(int a[3]) {
//    for (int^ r : a) {
//        r^ = 9;
//    }
//}

/* a DECLARED plain `int^` over CONST array elements drops the const — the
   declared-type mirror of the `ref^ :` reuse rule. */
//-EXPECT-ERROR: does not match the array element type 'const int'
//void neg_const_declared_ref() {
//    const int carr[3] = (1, 2, 3);
//    for (int^ r : carr) {
//        r^ = 9;
//    }
//}
