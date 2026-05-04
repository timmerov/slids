/*
develop syntax for ranged for.

valid container types for short-for:
range, enum, array, tuple, class.
caveats:
array must be one-dimensional.
tuple must be homogeneous - the same type.
it's literally cast to an array.
we iterate over a class by value or reference.
by value if op[] and size() are defined
by reference if begin(), end(), next() are defined.
if one of those sets are defined then we can
infer the loop var type.
if both are defined then the loop var declaration
must be explicit.
compile error if both sets return the same type.
*/

Simple(int x_ = 0) {
    _() {
        __println("Simple:ctor");
    }
    ~() {
        __println("Simple:dtor");
    }
}

Loop(
    int low_,
    int high_
) {
    int begin() {
        return low_;
    }

    int end() {
        return high_ + 1;
    }

    int next(int prev) {
        return prev + 1;
    }
}

enum Bases (
    kFirst,
    kSecond,
    kThird,
    kLast
)

int32 main() {

    /* long form for. */
    __print("for long form:");
    /* keyword */
    for
    /* declaration, initialization tuple. */
    /* infer, re-use, starts for scope. */
    (int x = 0, int y = 10)
    /* condition "tuple" aka parenthesized expression. */
    (x < y)
    /* update code block. */
    { ++x; --y; }
    /* loop body. */
    {
        __print(" (" + x + "," + y + ")");
    }
    /* optional label followed by ; default 'for' */
    :optional_label;
    __println();

    /* ctor/dtor test. */
    __println("for ctor.dtor: before");
    for (Simple s = 0) () {s.x_ = s.x_ + 1;} {
        break simple;
    } :simple;
    __println("for ctor.dtor: after");

    /*
    print the ints in the range.
    */
    __print("for range:");
    for (x : 0..10) {
        __print(" " + x);
    }
    __println();

    /*
    print the ints in the tuple.
    this might de-sugar to printing the
    elements of an array initialized by a tuple.
    desugars to:
    init:
        int $index = 0;
        int x;
    cond:
        $index < number of tuple elements
    update:
        ++$index;
    body:
        x = tuple_as_array[$index];
    */
    __print("for tuple:");
    for (x : (1, 1, 2, 3, 5, 8)) {
        __print(" " + x);
    }
    __println();

    /* compile error */
    //for (x : (1, 1, 2, "Hello")) {
    //    __println("compile error: tuple must be homogenous");
    //}

    /* print the characters in a string literal */
    __print("for string literal:");
    for (ch : "Hello") {
        __print(" " + ch);
    }
    __println();

    /* print the characters in a fixed size array */
    int array[6] = (1, 1, 2, 3, 5, 8);
    __print("for fixed-size array:");
    for (x : array) {
        __print(" " + x);
    }
    __println();

    /*
    for over an enum.
    loops from the first symbol defined
    to the last symbol defined, inclusive.
    desugars to:
    init:
        x = kFirst;
    condition:
        x != kLast;
    update:
        ++x;
    */
    __print("for enum:");
    for (x : Bases) {
        __print(" " + x);
    }
    __println();

    {
        __println("begin tuple block");
        tuple = (Simple, Simple);
        __println("tuple declared");

        /* compile error. uncomment to verify: caret on x, "cannot infer loop variable type for class-typed elements". */
        //for (x : tuple) {
        //    __println("compile error: cannot infer type iterating over class objects." );
        //}

        /*
        iterate by value.
        desugars to:
        init:
            int $index = 0;
            Simple x;
        cond:
            $index < number of tuple elements
        update:
            ++$index;
        body:
            x = tuple_as_array[$index];
        */
        __println("iterate by value: begin");
        for (Simple x : tuple) {
            __println("iterate by value: loop");
        }
        __println("iterate by value: end");

        /* iterate by reference.
        desugars to:
        init:
            int $index = 0;
            Simple^ x = ^tuple_as_array[0];
        cond:
            $index < number of tuple elements
        update:
            ++$index;
            x = ^tuple_as_array[$index];
        */
        for (Simple^ x : tuple) {
            __println("iterate by reference.");
        }
        __println("end tuple block");
    }
    __println("after tuple block");

    /* compile error. */
    //int board[8][8];
    //for (x : board) {
    //    __println("compile error: cannot iterate over a multi-dimensional array.");
    //}

    __println("Simple[3] begin: expect 3 ctor/dtor");
    {
        Simple array[3];
    }
    __println("Simple[3] end: expect 3 ctor/dtor");

    {
        Simple array[3];
        /* compile error. */
        //for (x : array) {
        //    __println("compile error: need explicit declaration: object or reference.");
        //}
        /* iterate by value. */
        __println("iterate Simple array by value: begin: expect 1 ctor/dtor");
        for (Simple x : array) {
            __println("array of slid: iterate by value.");
        }
        __println("iterate Simple array by value: end: expect 1 ctor/dtor");
        /* iterate by reference. */
        __println("iterate Simple array by reference: begin: expect 0 ctor/dtor");
        for (Simple^ x : array) {
            __println("array of slid: iterate by reference.");
        }
        __println("iterate Simple array by value: end: expect 0 ctor/dtor");
    }

    {
        /* compile error: cannot infer non-primitive element type. */
        //for (x : (Simple(1), Simple(2))) {
        //    __println("compile error: tuple-literal of slid needs explicit type.");
        //}
        /* iterate by value. */
        __println("iterate Simple tuple by value: begin: expect 3 ctor/dtor");
        for (Simple x : (Simple(1), Simple(2))) {
            __println("tuple-literal of slid: iterate by value.");
        }
        __println("iterate Simple tuple by value: end: expect 3 ctor/dtor");
        /* iterate by reference. */
        __println("iterate Simple tuple by reference: begin: expect 2 ctor/dtor");
        for (Simple^ x : (Simple(1), Simple(2))) {
            __println("tuple-literal of slid: iterate by reference.");
        }
        __println("iterate Simple tuple by reference: begin: expect 2 ctor/dtor");
    }

    /*
    iterate over a container class.
    desugars to:
    init:
        int a = lp.begin();
        int __$end_0 = lp.end();
    condition:
        a != __$end_0
    update:
        a = lp.next(a);
    */
    Loop loop(17, 21);
    __print("for container:");
    for (x : loop) {
        __print(" " + x);
    }
    __println();

    return 0;
}
