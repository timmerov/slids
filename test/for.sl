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

compile errors:
if short-for by reference is used, then...
begin, end, next must all return exactly the same type.
the next parameter must also be that type.
begin, end, next must have 0, 0, 1 parameters.

allowed:
begin, end, next are not required to conform.
they just can't be used for short-for.

open questions:
do we allow return type overloads in general?
do we allow multiple sets of begin or []? lean yes, defer.
*/

Simple(int x_ = 0) {
    _() {
        __println("Simple:ctor");
    }
    ~() {
        __println("Simple:dtor");
    }
}

enum Bases (
    kFirst,
    kSecond,
    kThird,
    kLast
)

BeginEndInt(
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

IndexSizeInt(
    int table_[3]
) {
    _() {
        /* workaround missing feature. */
        table_[0] = 13;
        table_[1] = 17;
        table_[2] = 19;
    }
    ~() {}

    int size() {
        return 3;
    }

    int op[](int index) {
        return table_[index];
    }
}

Flexible(
    int table_[3]
) {
    _() {
        /* workaround missing feature. */
        table_[0] = 13;
        table_[1] = 17;
        table_[2] = 19;
    }
    ~() {}

    int size() {
        return 3;
    }

    int op[](int index) {
        return table_[index];
    }

    int^ begin() {
        return ^table_[0];
    }

    int^ end() {
        return ^table_[3];
    }

    int^ next(int^ prev) {
        piter = <int[]> prev;
        return piter + 1;
    }
}

TypeMismatch1(
    int x_
) {
    int begin() {
        return 0;
    }

    int^ end() {
        return nullptr;
    }

    int^ next(int^ prev) {
        return nullptr;
    }
}

TypeMismatch2(
    int x_
) {
    int^ begin() {
        return nullptr;
    }

    int end() {
        return 0;
    }

    int^ next(int^ prev) {
        return nullptr;
    }
}

TypeMismatch3(
    int x_
) {
    int^ begin() {
        return nullptr;
    }

    int^ end() {
        return nullptr;
    }

    int next(int^ prev) {
        return 0;
    }
}

TypeMismatch4(
    int x_
) {
    int^ begin() {
        return nullptr;
    }

    int^ end() {
        return nullptr;
    }

    int^ next(int prev) {
        return nullptr;
    }
}

/* Incomplete by-value: op[] without size. */
IncompleteByValue(int x_) {
    int op[](int index) {
        return 0;
    }
}

/* Incomplete by-reference: begin and end but no next. */
IncompleteByRef(int x_) {
    int begin() {
        return 0;
    }
    int end() {
        return 0;
    }
}

/* Malformed by-value: size takes a parameter (should take 0). */
MalformedSize(int x_) {
    int size(int extra) {
        return 0;
    }
    int op[](int index) {
        return 0;
    }
}

/* Malformed by-reference: next takes 0 parameters (should take 1). */
MalformedNext(int x_) {
    int begin() {
        return 0;
    }
    int end() {
        return 0;
    }
    int next() {
        return 0;
    }
}

/* Not iterable: no protocol methods. */
NotIterable(int x_) {}

/* Both Good with identical return types — cannot disambiguate. */
BothSame(int x_) {
    int size() {
        return 0;
    }
    int op[](int index) {
        return 0;
    }
    int begin() {
        return 0;
    }
    int end() {
        return 0;
    }
    int next(int prev) {
        return prev + 1;
    }
}

/* By-value Good, by-reference malformed: iterates via op[]/size; begin/end/next overlooked. */
GoodValueBadRef(int x_) {
    int size() {
        return 3;
    }
    int op[](int index) {
        return 100 + index;
    }
    int begin() {
        return 0;
    }
    int end() {
        return 0;
    }
    int next() {
        return 0;
    }
}

/* By-reference Good, by-value malformed: iterates via begin/end/next; op[]/size overlooked. */
BadValueGoodRef(int x_) {
    int size(int extra) {
        return 0;
    }
    int op[](int index) {
        return 0;
    }
    int begin() {
        return 200;
    }
    int end() {
        return 203;
    }
    int next(int prev) {
        return prev + 1;
    }
}

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

    //-EXPECT-ERROR-DEFERRED: heterogeneous tuple element-typing not validated as a focused diagnostic
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

        //-EXPECT-ERROR: cannot infer loop type variable
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

    //-EXPECT-ERROR: multi-dimensional fixed-size array iteration not supported
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
        //-EXPECT-ERROR: cannot infer loop type variable
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
        //-EXPECT-ERROR: cannot infer loop type variable
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

    {
        /* anon-tuple element type — same inference rule as slid elements. */

        //-EXPECT-ERROR: cannot infer loop type variable
        //for (x : ((1, 2), (3, 4))) {
        //    __println("compile error: tuple-literal of anon-tuple needs explicit type.");
        //}

        //-EXPECT-ERROR-DEFERRED: parser does not yet accept anon-tuple-element fixed-array decls
        //(int, int) pairs[3] = ((1, 2), (3, 4), (5, 6));
        //for (x : pairs) {
        //    __println("compile error: fixed-array of anon-tuple needs explicit type.");
        //}

        //-EXPECT-ERROR: cannot infer loop type variable
        //triples = ((1, 2, 3), (4, 5, 6));
        //for (x : triples) {
        //    __println("compile error: tuple-named of anon-tuple needs explicit type.");
        //}
    }

    {
        /*
        iterate over a container class with begin and end, infer type.
        desugars to:
        init:
            int iter = container.begin();
            int __$end_0 = container.end();
        condition:
            iter != __$end_0
        update:
            iter = container.next(a);
        */
        BeginEndInt container(17, 21);
        __print("for container by reference:");
        for (iter : container) {
            __print(" " + iter);
        }
        __println();
    }

    {
        /*
        iterate over a container class with index and size, infer type.
        desugars to:
        init:
            int value;
            int __$index_0 = 0;
            int __$end_0 = container.size();
        condition:
            __$index_0 < __$end_0
        update:
            __$index_0 += 1;
        body:
            value = container[__$index];
        */
        IndexSizeInt container;
        __print("for container by value:");
        for (value : container) {
            __print(" " + value);
        }
        __println();
    }

    {
        /* iterate over a container class with both index and size. */
        Flexible container;

        //-EXPECT-ERROR: defines both op[]/size and begin/end/next; explicit loop var type required
        //for (x : container) {
        //    __print("compile error: cannot infer type.");
        //}

        __print("for container by value:");
        for (int value : container) {
            __print(" " + value);
        }
        __println();

        __print("for container by reference:");
        for (int^ iter : container) {
            __print(" " + iter^);
        }
        __println();
    }

    /* compile errors. */
    TypeMismatch1 mismatch1;
    TypeMismatch2 mismatch2;
    TypeMismatch3 mismatch3;
    TypeMismatch4 mismatch4;
    //-EXPECT-ERROR: 'TypeMismatch1' begin/end/next return types differ
    //for (x : mismatch1) {
    //    __print("compile error: begin/end/next must all use the same type.");
    //}
    //-EXPECT-ERROR: 'TypeMismatch2' begin/end/next return types differ
    //for (x : mismatch2) {
    //    __print("compile error: begin/end/next must all use the same type.");
    //}
    //-EXPECT-ERROR: 'TypeMismatch3' begin/end/next return types differ
    //for (x : mismatch3) {
    //    __print("compile error: begin/end/next must all use the same type.");
    //}
    //-EXPECT-ERROR: 'TypeMismatch4' next parameter type
    //for (x : mismatch4) {
    //    __print("compile error: begin/end/next must all use the same type.");
    //}

    {
        IncompleteByValue iv;
        IncompleteByRef   ir;
        MalformedSize     ms;
        MalformedNext     mn;
        NotIterable       ni;
        BothSame          bs;
        Flexible          fx;

        //-EXPECT-ERROR: 'IncompleteByValue' defines op[] but not size
        //for (x : iv) {
        //    __print("compile error: incomplete by-value protocol.");
        //}

        //-EXPECT-ERROR: 'IncompleteByRef' defines some of begin/end/next but not all
        //for (x : ir) {
        //    __print("compile error: incomplete by-reference protocol.");
        //}

        //-EXPECT-ERROR: 'MalformedSize' size must take 0 parameters
        //for (x : ms) {
        //    __print("compile error: malformed size arity.");
        //}

        //-EXPECT-ERROR: 'MalformedNext' next must take 1 parameter
        //for (x : mn) {
        //    __print("compile error: malformed next arity.");
        //}

        //-EXPECT-ERROR: 'NotIterable' is not iterable
        //for (x : ni) {
        //    __print("compile error: not iterable.");
        //}

        //-EXPECT-ERROR: 'BothSame' defines both op[]/size and begin/end/next with identical return types
        //for (int x : bs) {
        //    __print("compile error: both protocols return identical type.");
        //}

        //-EXPECT-ERROR: matches neither op[] return
        //for (bool x : fx) {
        //    __print("compile error: explicit type matches neither protocol return.");
        //}
    }

    {
        /* Good protocol present alongside a malformed one: spec says the
           non-conforming protocol is silently overlooked. */
        GoodValueBadRef gvbr;
        __print("for good-by-value, bad-by-ref:");
        for (x : gvbr) {
            __print(" " + x);
        }
        __println();

        BadValueGoodRef bvgr;
        __print("for bad-by-value, good-by-ref:");
        for (x : bvgr) {
            __print(" " + x);
        }
        __println();
    }

    return 0;
}
