/*
test for loop over a class.
by value and by reference.

    for ( var : container ) { body }
    for ( ref : container ) { body }

the loop variable can be a primitive type or a reference.
the loop variable type may be inferred.
if the contained type is a primitive then the loop variable is primitive.
otherwise it's a reference to the contained type.
an explicit loop variable type must be compatible with the contained type.

the container class must implement all of begin/end/next
or both of size/op[].
begin/end/next must return the exact same type.
begin/end/next must have arity 0/0/1.
the loop variable type must be explicit if the class implements both sets
of loop functions.
if the loop variable is the contained type then size/op[] is used.
if the loop variable is a reference to the contained type then
begin/end/next is used.
the loop functions are not rejected if they are malformed.
they just cannot be used with for-class.

for-class desugars to:

    /* by-value when begin/end/next return value */
    for (
        Primitive var = container.begin(),
        Primitive $end = container.end()
    ) (
        var != $end
    ) {
        var = container.next(var);
    } {
        body
    }

    /* by-value when begin/end/next return reference */
    for (
        Primitive^ $ref = container.begin(),
        Primitive^ $end = container.end(),
        Primitive var
    ) (
        $ref != $end
    ) {
        $ref = container.next($ref);
    } {
        var = $ref^;
        body
    }

    /* by-reference with begin/end/next */
    for (
        Type^ ref = container.begin(),
        Type^ $end = container.end()
    ) (
        ref != $end
    ) {
        ref = container.next(ref);
    } {
        body
    }

    /* by-value with size/op[] */
    for (
        Integer $size = container.size(),
        Integer $count = 0,
        Primitive var
    ) (
        $count < $size
    ) {
        $count += 1;
    } {
        var = container.op[]($count)^;
        body
    }

    /* by-reference with size/op[] */
    for (
        Integer $size = container.size(),
        Integer $count = 0,
        Type^ ref
    ) (
        $count < $size
    ) {
        $count += 1;
    } {
        ref = container.op[]($count);
        body
    }

note:
loop code can be injected into the loop's body.

for the purposes of shadowing variables, there are 3 scopes counting the
enclosing scope:
normal local variable shadowing rules for scopes apply to these scopes.

    |--enclosing---------------|
    { for (var : class) {body} }
                        |body|
          |--loop-var--------|
*/

// size/op[] — op[] returns a reference to a field; iterated by value (a copy) or
// by reference (write-through). Three int elements.
IdxVec(int a_ = 1, int b_ = 2, int c_ = 3) {
    int size() {
        return 3;
    }
    int^ op[](int i) {
        if (i == 0) { return ^a_; }
        if (i == 1) { return ^b_; }
        return ^c_;
    }
}

// begin/end/next returning a VALUE — the loop variable IS the iterator value.
Count(int n_ = 3) {
    int begin() {
        return 0;
    }
    int end() {
        return n_;
    }
    int next(int i) {
        return i + 1;
    }
}

// begin/end/next returning a REFERENCE — the iterator is a `int^` into the fields;
// next advances it by round-tripping through a mutable iterator. Iterated by value
// (deref) or by reference (the loop var IS the element reference).
Buf3(int b0_ = 5, int b1_ = 6, int b2_ = 7) {
    int^ begin() {
        return ^b0_;
    }
    int^ end() {
        int[] p = <int[]> <mutable> ^b0_;
        return p + 3;
    }
    int^ next(int^ prev) {
        int[] p = <int[]> <mutable> prev;
        return p + 1;
    }
}

// BOTH protocols — size/op[] over all three fields (a by-value loop var selects it),
// begin/end/next as a reference iterator over the FIRST TWO (a reference loop var
// selects it). The differing lengths make the selection observable.
Both(int a_ = 1, int b_ = 2, int c_ = 3) {
    int size() {
        return 3;
    }
    int^ op[](int i) {
        if (i == 0) { return ^a_; }
        if (i == 1) { return ^b_; }
        return ^c_;
    }
    int^ begin() {
        return ^a_;
    }
    int^ end() {
        int[] p = <int[]> <mutable> ^a_;
        return p + 2;
    }
    int^ next(int^ prev) {
        int[] p = <int[]> <mutable> prev;
        return p + 1;
    }
}

// A class with NO iteration protocol (used by a negative below).
Plain(int a_ = 1) {
    int get() {
        return a_;
    }
}

// A class with a PARTIAL protocol — size without op[] (used by a negative below).
Half(int a_ = 1) {
    int size() {
        return 1;
    }
}

// Returns an IdxVec by value — a call-form (rvalue) container.
IdxVec makeVec() {
    IdxVec v = (4, 5, 6);
    return v;
}

// An observable-lifetime container (printing ctor/dtor): proves a spilled rvalue
// container is constructed ONCE and destructed at loop-scope exit, not leaked.
Obj(int a_ = 70, int b_ = 80) {
    _() {
        __println("  Obj:ctor");
    }
    ~() {
        __println("  Obj:dtor");
    }
    int size() {
        return 2;
    }
    int^ op[](int i) {
        if (i == 0) { return ^a_; }
        return ^b_;
    }
}
Obj makeObj() {
    Obj o;
    return o;
}

// size() == 0 — an empty iteration must skip the body entirely.
Empty(int dummy_ = 0) {
    int size() {
        return 0;
    }
    int^ op[](int i) {
        return ^dummy_;
    }
}

// A malformed (unusable) size/op[] set — a lone size — alongside a WORKING
// begin/end/next: the malformed set is ignored and iteration uses begin/end/next.
Mixed(int b0_ = 5, int b1_ = 6, int b2_ = 7) {
    int size() {
        return 99;
    }
    int^ begin() {
        return ^b0_;
    }
    int^ end() {
        int[] p = <int[]> <mutable> ^b0_;
        return p + 3;
    }
    int^ next(int^ prev) {
        int[] p = <int[]> <mutable> prev;
        return p + 1;
    }
}

// class ELEMENTS — op[] returns a reference to a class; an inferred loop variable
// binds by reference (the element is non-primitive) and is dereferenced to read.
Pair(int x_ = 0, int y_ = 0) { }
PairVec(Pair p_ = (1, 2), Pair q_ = (3, 4)) {
    int size() {
        return 2;
    }
    Pair^ op[](int i) {
        if (i == 0) { return ^p_; }
        return ^q_;
    }
}

// Inheritance — a derived class iterates via the base's inherited size/op[].
IdxBase(int a_ = 100, int b_ = 200) {
    int size() {
        return 2;
    }
    int^ op[](int i) {
        if (i == 0) { return ^a_; }
        return ^b_;
    }
}
IdxBase : IdxDerived(int extra_ = 0) { }

// A global class container.
global IdxVec gvec = (11, 22, 33);

int32 main() {
    // ---- size/op[] ----
    IdxVec v;
    __println("op[] by value:");
    for (int x : v) {
        __println("  " + x);
    }
    __println("op[] by reference (x10):");
    for (int^ r : v) {
        r^ = r^ * 10;
    }
    for (int x : v) {
        __println("  " + x);
    }

    // ---- begin/end/next returning a value ----
    Count c;
    __println("begin/end/next (value):");
    for (int i : c) {
        __println("  " + i);
    }

    // ---- begin/end/next returning a reference ----
    Buf3 b;
    __println("begin/end/next (ref) by value:");
    for (int x : b) {
        __println("  " + x);
    }
    __println("begin/end/next (ref) by reference (+100):");
    for (int^ r : b) {
        r^ = r^ + 100;
    }
    for (int x : b) {
        __println("  " + x);
    }

    // ---- both protocols: the loop-var shape selects ----
    Both both;
    __println("both, value loop var -> op[] (3 elements):");
    for (int x : both) {
        __println("  " + x);
    }
    __println("both, ref loop var -> begin/end/next (2 elements, +1000):");
    for (int^ r : both) {
        r^ = r^ + 1000;
    }
    for (int x : both) {
        __println("  " + x);
    }

    // ---- expression containers: a deref, a construction, a call ----
    IdxVec base;
    IdxVec^ bp = ^base;
    __println("container via ptr^:");
    for (int x : bp^) {
        __println("  " + x);
    }
    __println("container via construction:");
    for (int x : IdxVec(40, 50, 60)) {
        __println("  " + x);
    }
    __println("container via call:");
    for (int x : makeVec()) {
        __println("  " + x);
    }

    // ---- a spilled rvalue container is built once, destructed at loop exit ----
    __println("spilled container lifetime:");
    __println("before");
    for (int x : makeObj()) {
        __println("  x=" + x);
    }
    __println("after");

    // ---- nested for-class: the synthesized locals get unique names per loop ----
    IdxVec n1;
    IdxVec n2;
    __println("nested:");
    for (int i : n1) {
        for (int j : n2) {
            __println("  " + i + "," + j);
        }
    }

    // ---- inferred loop-variable type ----
    IdxVec iv;
    __println("inferred (primitive -> by value):");
    for (x : iv) {
        __println("  " + x);
    }
    __println("inferred (begin/end/next value):");
    Count ic;
    for (i : ic) {
        __println("  " + i);
    }
    __println("inferred (class element -> by reference):");
    PairVec pv;
    for (r : pv) {
        __println("  " + r^.x_ + "," + r^.y_);
    }

    // ---- a malformed protocol is ignored when another is usable ----
    __println("malformed-but-unused (uses begin/end/next):");
    Mixed mx;
    for (int x : mx) {
        __println("  " + x);
    }

    // ---- loop-variable widening (int -> int64) ----
    __println("widening to int64:");
    for (int64 x : iv) {
        __println("  " + x);
    }

    // ---- break / continue ----
    __println("break at 2:");
    for (int x : iv) {
        if (x == 2) {
            break;
        }
        __println("  " + x);
    }
    __println("continue at 2:");
    for (int x : iv) {
        if (x == 2) {
            continue;
        }
        __println("  " + x);
    }
    __println("labeled break to the outer loop:");
    for (int i : n1) {
        for (int x : iv) {
            if (x == 2) {
                break outer;
            }
            __println("  " + i + "," + x);
        }
    } :outer;

    // ---- empty iteration (size 0) skips the body ----
    __println("empty:");
    Empty ev;
    for (int x : ev) {
        __println("  SHOULD NOT APPEAR " + x);
    }
    __println("empty done");

    // ---- inherited protocol ----
    __println("inherited size/op[]:");
    IdxDerived d;
    for (int x : d) {
        __println("  " + x);
    }

    // ---- an array-element container and a global container ----
    IdxVec arr[2];
    __println("array-element container:");
    for (int x : arr[0]) {
        __println("  " + x);
    }
    __println("global container:");
    for (int x : gvec) {
        __println("  " + x);
    }

    // ---- low: a by-ref write into a spilled (discarded) container just runs ----
    __println("by-ref write into a spilled container:");
    for (int^ r : makeVec()) {
        r^ = 0;
    }
    __println("spilled write done");

    return 0;
}

// A class defining BOTH protocols requires an explicit loop-variable type.
//-EXPECT-ERROR: must be written explicitly to select a protocol
//int neg_both_inferred() {
//    Both both;
//    for (x : both) {
//        return x^;
//    }
//    return 0;
//}

// A class defining NEITHER protocol is not iterable.
//-EXPECT-ERROR: is not iterable
//int neg_not_iterable() {
//    Plain p;
//    for (int x : p) {
//        return x;
//    }
//    return 0;
//}

// A partial size/op[] set (size without op[]) cannot form the protocol.
//-EXPECT-ERROR: defines size but not op[]
//int neg_partial_protocol() {
//    Half h;
//    for (int x : h) {
//        return x;
//    }
//    return 0;
//}

// The mirror partial — op[] without size.
//-EXPECT-ERROR: defines op[] but not size
//OpOnly(int a_ = 1) {
//    int^ op[](int i) {
//        return ^a_;
//    }
//}
//int neg_op_without_size() {
//    OpOnly o;
//    for (int x : o) {
//        return x;
//    }
//    return 0;
//}

// A partial begin/end/next set (missing next).
//-EXPECT-ERROR: some of begin/end/next but not all
//PartBnn(int a_ = 1) {
//    int begin() {
//        return 0;
//    }
//    int end() {
//        return 1;
//    }
//}
//int neg_partial_bnn() {
//    PartBnn p;
//    for (int x : p) {
//        return x;
//    }
//    return 0;
//}

// begin/end/next with the wrong arity (next takes two parameters).
//-EXPECT-ERROR: must have arity 0/0/1
//BadArity(int a_ = 1) {
//    int begin() {
//        return 0;
//    }
//    int end() {
//        return 1;
//    }
//    int next(int i, int j) {
//        return i + j;
//    }
//}
//int neg_bad_arity() {
//    BadArity b;
//    for (int x : b) {
//        return x;
//    }
//    return 0;
//}

// begin/end/next returning differing types.
//-EXPECT-ERROR: must all return, and next must take, the same type
//DiffRet(int a_ = 1) {
//    int begin() {
//        return 0;
//    }
//    int64 end() {
//        return 1;
//    }
//    int next(int i) {
//        return i;
//    }
//}
//int neg_diff_return() {
//    DiffRet d;
//    for (int x : d) {
//        return x;
//    }
//    return 0;
//}

// op[] must return a reference, not a value.
//-EXPECT-ERROR: op[] on type 'ValOp' must return a reference
//ValOp(int a_ = 1) {
//    int size() {
//        return 1;
//    }
//    int op[](int i) {
//        return a_;
//    }
//}
//int neg_op_value() {
//    ValOp v;
//    for (int x : v) {
//        return x;
//    }
//    return 0;
//}

// A reference loop variable over a value-returning begin/end/next is rejected.
//-EXPECT-ERROR: the for-loop variable cannot be a reference
//int neg_ref_over_value() {
//    Count c;
//    for (int^ r : c) {
//        return r^;
//    }
//    return 0;
//}

// A narrowing loop-variable type (int element into int8) is a compile error.
//-EXPECT-ERROR: Cannot implicitly narrow
//int neg_truncating_loop_var() {
//    IdxVec v;
//    for (int8 x : v) {
//        return x;
//    }
//    return 0;
//}

// The loop variable is swept like any local — an unused one is rejected (a
// for-class loop var lowers to an ordinary for-long var, so it is not exempt).
//-EXPECT-ERROR: set but never used
//int neg_unused_loop_var() {
//    IdxVec v;
//    for (int x : v) {
//    }
//    return 0;
//}
