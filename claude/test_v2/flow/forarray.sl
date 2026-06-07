/*
test for over a fixed size array.

array must be one dimensional.
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
        x = arr[_$idx#],
        /*body*/
    }

note:
the compiler inserts instructions at the start of the loop body.

*/

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

    return 0;
}

/* the array must be one-dimensional. */
//-EXPECT-ERROR: requires a one-dimensional array
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
