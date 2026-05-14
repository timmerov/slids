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
op[] returns a reference to the element; the loop
binding reads (T) or writes (T^) through it.
if neither set is defined, compile error.
if exactly one set is defined, it handles both
T and T^ loop-var shapes.
if both sets are defined:
- inferred loop var: compile error.
- explicit T loop var: pick op[]/size.
- explicit T^ loop var: pick begin/end/next.
loop var type must be compatible with the picked
set's element type. integer and float widening is
allowed; truncation is a compile error. an
implementation that is incompatible with the loop
var is treated as if it weren't there.

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

    int^ op[](int index) {
        return ^table_[index];
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

    int^ op[](int index) {
        return ^table_[index];
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
    int^ op[](int index) {
        return ^x_;
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
    int^ op[](int index) {
        return ^x_;
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

/* Both Good — inferred loop var cannot disambiguate. */
BothSame(int x_) {
    int size() {
        return 0;
    }
    int^ op[](int index) {
        return ^x_;
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
    int^ op[](int index) {
        return ^x_;
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
    int^ op[](int index) {
        return ^x_;
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

    //-EXPECT-ERROR: Variable index on heterogeneous tuple
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

        //-EXPECT-ERROR: For-loop cannot infer the loop variable type
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

    //-EXPECT-ERROR: Short-form for-loops do not support multi-dimensional fixed-size array iteration
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
        //-EXPECT-ERROR: For-loop cannot infer the loop variable type
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
        //-EXPECT-ERROR: For-loop cannot infer the loop variable type
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

        //-EXPECT-ERROR: For-loop cannot infer the loop variable type
        //for (x : ((1, 2), (3, 4))) {
        //    __println("compile error: tuple-literal of anon-tuple needs explicit type.");
        //}

        //-EXPECT-ERROR: For-loop cannot infer the loop variable type
        //(int, int) pairs[3] = ((1, 2), (3, 4), (5, 6));
        //for (x : pairs) {
        //    __println("compile error: fixed-array of anon-tuple needs explicit type.");
        //}

        //-EXPECT-ERROR: For-loop cannot infer the loop variable type
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

        //-EXPECT-ERROR: defines both op[]/size and begin/end/next; the for-iterator loop variable type must be written explicitly
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
    //-EXPECT-ERROR: Methods begin/end/next on type 'TypeMismatch1' return differing types
    //for (x : mismatch1) {
    //    __print("compile error: begin/end/next must all use the same type.");
    //}
    //-EXPECT-ERROR: Methods begin/end/next on type 'TypeMismatch2' return differing types
    //for (x : mismatch2) {
    //    __print("compile error: begin/end/next must all use the same type.");
    //}
    //-EXPECT-ERROR: Methods begin/end/next on type 'TypeMismatch3' return differing types
    //for (x : mismatch3) {
    //    __print("compile error: begin/end/next must all use the same type.");
    //}
    //-EXPECT-ERROR: Parameter type 'int' of next on type 'TypeMismatch4' must match the begin/end/next return type 'int^'
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

        //-EXPECT-ERROR: Method size on type 'MalformedSize' must take 0 parameters; got 1
        //for (x : ms) {
        //    __print("compile error: malformed size arity.");
        //}

        //-EXPECT-ERROR: Method next on type 'MalformedNext' must take 1 parameter; got 0
        //for (x : mn) {
        //    __print("compile error: malformed next arity.");
        //}

        //-EXPECT-ERROR: 'NotIterable' is not iterable
        //for (x : ni) {
        //    __print("compile error: not iterable.");
        //}

        //-EXPECT-ERROR: 'BothSame' defines both op[]/size and begin/end/next; the for-iterator loop variable type must be written explicitly
        //for (x : bs) {
        //    __print("compile error: inferred loop var when both protocols defined.");
        //}

        //-EXPECT-ERROR: For-iterator loop variable type 'bool' is not compatible
        //for (bool x : fx) {
        //    __print("compile error: explicit type incompatible with both protocols.");
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

    /*
    short-form reuse semantics: an untyped loop-var name reuses an in-scope
    outer local; spelled type always shadows. value persists after the loop.
    */
    {
        int x = 99;
        for (x : 0..3) {}
        __println("reuse range: " + x);
    }

    {
        int x = 99;
        for (int x : 0..3) {}
        __println("shadow typed: " + x);
    }

    {
        int x = 99;
        for (x : (10, 20, 30)) {}
        __println("reuse tuple: " + x);
    }

    {
        char ch = '?';
        for (ch : "ABC") {}
        __println("reuse string: " + ch);
    }

    {
        int arr[3] = (10, 20, 30);
        int x = 99;
        for (x : arr) {}
        __println("reuse array: " + x);
    }

    {
        int x = 99;
        for (x : Bases) {}
        __println("reuse enum: " + x);
    }

    {
        IndexSizeInt container;
        int value = 99;
        for (value : container) {}
        __println("reuse class by-value: " + value);
    }

    {
        /* find-and-break — chess1 pattern in miniature. */
        int board[3][3] = (
            (0, 0, 0),
            (0, 0, 1),
            (0, 0, 0)
        );
        int row = 99;
        int col = 99;
        for (row : 0..3) {
            for (col : 0..3) {
                if (board[row][col] == 1) {
                    break rows;
                }
            } :cols;
        } :rows;
        __println("reuse find-and-break: row=" + row + " col=" + col);
    }

    return 0;
}
