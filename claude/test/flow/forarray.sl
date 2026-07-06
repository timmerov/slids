/*
test for over a fixed size array.

to iterate by value, the array must be a primitive type.
all types may be iterated by reference.
loop variables are to be re-used if they exist in enclosing scope.
the loop variable type may be inferred.
in case of ambiguity, loop by value.

    int arr[5];

    /* iterate by reference */
    for (int^ iter : arr) {
        iter^ = 77;
    }

    /* iterate by value. */
    for (int x : arr) {
        __println("x = " + x);
    }

    /* iterate by value. */
    for (x : arr) {
        __println("x = " + x);
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
*/

alias Cell = int;

int32 main() {
    int arr[5];

    /* by reference: fill the array through element references. */
    int n = 0;
    for (int^ iter : arr) {
        iter^ = n * n;
        n = n + 1;
    }
    __println("arr[0]= " + arr[0]);        // 0
    __println("arr[3]= " + arr[3]);        // 9

    /* by value, typed loop variable. */
    int sum = 0;
    for (int x : arr) {
        sum = sum + x;
    }
    __println("sum= " + sum);              // 30

    /* by value, typeless loop variable (inferred element type, fresh local). */
    for (y : arr) {
        __println("y= " + y);              // 0, 1, 4, 9, 16
    }

    /* typeless loop variable reuses an enclosing local — observable after. */
    int last = 0;
    for (last : arr) {
    }
    __println("last= " + last);            // 16

    /* break / continue work in a for-array body. */
    int found = -1;
    for (int x : arr) {
        if (x == 4) {
            found = x;
            break;
        }
    }
    __println("found= " + found);          // 4

    /* a labeled for-array, broken by name. */
    int count = 0;
    for (int x : arr) {
        count = count + 1;
        if (x == 4) {
            break scan;
        }
    } :scan;
    __println("count= " + count);          // 3

    /* continue skips an element. */
    int csum = 0;
    for (int x : arr) {
        if (x == 4) {
            continue;
        }
        csum = csum + x;
    }
    __println("csum= " + csum);            // 0+1+9+16 = 26

    /* a naked continue. */
    int ncsum = 0;
    for (int x : arr) {
        if (x == 0) {
            continue;
        }
        ncsum = ncsum + x;
    }
    __println("ncsum= " + ncsum);          // 1+4+9+16 = 30

    /* by reference, read only (no write-back). */
    int rsum = 0;
    for (int^ p : arr) {
        rsum = rsum + p^;
    }
    __println("rsum= " + rsum);            // 30

    /* non-int element types, by value: int64, char, float. */
    int64 big[3] = (100, 200, 300);
    int64 bsum = 0;
    for (int64 v : big) {
        bsum = bsum + v;
    }
    __println("bsum= " + bsum);            // 600

    char letters[3] = ('a', 'b', 'c');
    for (char c : letters) {
        __print("" + c);
    }
    __println("");                         // abc

    float fs[3] = (1.5, 2.5, 3.0);
    float fsum = 0.0;
    for (float f : fs) {
        fsum = fsum + f;
    }
    __println("fsum= " + fsum);            // 7

    /* an alias element type, iterated by reference. */
    Cell cells[3] = (5, 6, 7);
    int asum = 0;
    for (Cell^ p : cells) {
        asum = asum + p^;
    }
    __println("asum= " + asum);            // 18

    /* nested for-array. */
    int a2[2] = (10, 20);
    int b2[3] = (1, 2, 3);
    int xsum = 0;
    for (x : a2) {
        for (y : b2) {
            xsum = xsum + x + y;
        }
    }
    __println("xsum= " + xsum);            // 36 + 66 = 102

    /* a numbered break exits N enclosing for-arrays at once. */
    int firstpair = -1;
    for (x : a2) {
        for (y : b2) {
            firstpair = x + y;
            break 2;
        }
    }
    __println("firstpair= " + firstpair);  // 10 + 1 = 11

    /* a typeless loop variable reuses a WIDER enclosing local (int -> int64). */
    int64 wlast = 0;
    for (wlast : arr) {
    }
    __println("wlast= " + wlast);          // 16

    /* a single-element array. */
    int one[1];
    one[0] = 42;
    int osum = 0;
    for (v : one) {
        osum = osum + v;
    }
    __println("osum= " + osum);            // 42

    /* nested for over 2d array. */
    int a3[2,3] = ((1,2), (3,4), (5,6));
    __print("a3=(");
    for (sub : a3) {
        __print(" (");
        for (x : sub^) {
            __print(" " + x);
        }
        __print(" )");
    }
    __println(" )");

    {
        Class(int x_) { }
        Class arr[3] = (1,2,3);
        __print("arr=[");
        for (ref : arr) {
            __print(" " + ref^.x_);
        }
        __println(" ]");
    }

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
//-EXPECT-ERROR: is not an enum, array, or tuple
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
//        __println("" + x);
//    }
//    return 0;
//}

/* reading an array BY VALUE before any write is use-before-init — a by-reference
   loop FILLS the array (no init required), a by-value loop READS it (init required). */
//-EXPECT-ERROR: Use of uninitialized variable 'arr'
//void neg_value_uninit() {
//    int arr[5];
//    for (int x : arr) {
//        __println("" + x);
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
