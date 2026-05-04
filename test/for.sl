/*
develop syntax for ranged for.
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

    return 0;
}
