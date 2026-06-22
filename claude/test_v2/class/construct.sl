/*
test class construction.

    Class (field-list) {body}

the constructor (ctor) is called immediately after initialization (see initialize.sl).
the destructor (dtor) is called at end of scope.
the ctor and dtor are called exactly once each.
ctor/dtor are hooks executed at the start and end of the object's scope.
ctor/dtor have no author define-able parameters.

ctor/dtor are optional.
they must appear together.
defining one without the other is a compile error.
the compiler will generate default (possibly) ctor/dtor if the author does not.

example declaration:

    Class(int f1_, int f2_) {
        _() {
            __println("Class:ctor");
        }
        ~() {
            __println("Class:dtor");
        }
    }

example instantiation:

    Class cls;

notes:

naming conventions are optional.
they are never used to resolve parse.

this file covers construction/destruction.
other class features are hendled elsewhere.
*/

/*
claude says:

ctor/dtor are NOT the constructor — fields are initialized first (see
initialize.sl), then the ctor runs as a hook, the dtor as a hook at scope exit.
they are call-if-needed: a trivial class (no hooks and no hook-carrying field)
emits no calls at all; a synthesized ctor/dtor exists only to run a field's
hooks in declaration / reverse-declaration order. forward declarations are
allowed but must be defined. the destructor-balance invariant — every instance
destroyed once, in reverse declaration order, on every exit (block end /
return / break / continue) — rides a scope chain threaded through codegen
alongside the loop context, gated by completion so we never emit after a
terminator.
*/

CtorDtor(int c_) {
    _() {
        __println("CtorDtor:ctor: " + c_);
    }
    ~() {
        __println("CtorDtor:dtor: " + c_);
    }
}

ForwardCtorDtor(int d_) {
    /* ctor/dtor forward declaration. */
    _();
    ~();

    /* ctor/dtor forward re-declaration. */
    _();
    ~();

    /* ctor/dtor definition. */
    _() {
        __println("ForwardCtorDtor:ctor: " + d_);
    }
    ~() {
        __println("ForwardCtorDtor:dtor: " + d_);
    }

    /* ctor/dtor aft-ward declaration. weird but allowed. */
    _();
    ~();
}

SynthesizedCtorDtor(
    CtorDtor field1_,
    CtorDtor field12_
) {
    /* no explicit ctor/dtor. */
}

/*
a 3-deep hook chain. each level has BOTH an explicit ctor/dtor AND a class-typed
field — so a field's ctor runs before the level's own ctor, and the level's own
dtor runs before the field's dtor. construction descends Leaf->Middle->Deep;
destruction unwinds the mirror Deep->Middle->Leaf.
*/
Leaf(int v_) {
    _() { __println("Leaf:ctor " + v_); }
    ~() { __println("Leaf:dtor " + v_); }
}
Middle(Leaf leaf_) {
    _() { __println("Middle:ctor"); }
    ~() { __println("Middle:dtor"); }
}
Deep(Middle mid_) {
    _() { __println("Deep:ctor"); }
    ~() { __println("Deep:dtor"); }
}

/* class with field class defined later. */
Now(Later later_) {
    _() { __println("Now:ctor: " + later_.x_); }
    ~() { __println("Now:dtor: " + later_.x_); }
}
Later(int x_) {
    _() { __println("Later:ctor: " + x_); }
    ~() { __println("Later:dtor: " + x_); }
}

int32 main() {

    /*
    ensure a class with no explicit ctor/dtor
    has a synthesized ctor/dtor that calls the
    ctors of its fields in declaration order
    and the dtors of its fields in reversed order.
    */
    {
        __println("expect ctors 100,200 after.");

        SynthesizedCtorDtor scd(100, 200);

        __println("expect ctors 100,200 before and dtors 200,100 after.");
    }
    __println("expect dtors 200,100 before.");

    /*
    a 3-deep chain where every level has an explicit ctor/dtor AND a hook field.
    Deep dp(5) threads 5 down to Leaf.v_. ctors run innermost-out
    (Leaf, Middle, Deep); dtors run the mirror (Deep, Middle, Leaf).
    */
    {
        __println("expect Leaf/Middle/Deep ctors after.");
        Deep dp(5);
        __println("expect ctors above, dtors Deep/Middle/Leaf below.");
    }
    __println("expect dtors above.");

    /* ctor/dtor order. */
    {
        __println("expect ctors 1,2,3 below.");
        CtorDtor cd1(1);
        CtorDtor cd2(2);
        CtorDtor cd3(3);
        __println("expect ctors 1,2,3 above and dtors 3,2,1 below.");
    }
    __println("expect dtors 3,2,1 above.");

    {
        ForwardCtorDtor fcd1 = 57;
    }

    /* array of ctor classes. */
    {
        __println("ctors 36,37,38 after.");
        CtorDtor arr[3] = (36, 37, 38);
        __println("ctors 36,37,38 before dtors 38,37,36 after.");
    }
    __println("dtors 38,37,36 before.");

    /* tuple of ctor classes. */
    /* deferred.
    {
        __println("ctors 14,24,34 after.");
        tuple = (CtorDtor(14), CtorDtor(24), CtorDtor(34));
        __println("ctors 14,24,34 before dtors 34,24,14 after.");
    }
    __println("dtors 34,24,14 before.");
    */

    /* nameless class. */
    /* deferred.
    {
        __println("expect ctors 11,12,13 below.");
        CtorDtor(11);
        CtorDtor(12);
        CtorDtor(13);
        __println("expect ctors 11,12,13 above and dtors 13,12,11 below.");
    }
    __println("expect dtors 13,12,11 above.");
    */

    {
        Now now(97);
    }

    return 0;
}

/*
negative cases. each block below is disabled; the negative-test runner enables
one at a time and asserts the marked error substring.
*/

//-EXPECT-ERROR: non-class value
//int32 neg_non_class() {
//    int n = 3;
//    int z = n.a_;
//    return z;
//}

//-EXPECT-ERROR: requires a matching destructor
//NoDtor(int x_) {
//    _() {
//    }
//}

//-EXPECT-ERROR: requires a matching constructor
//NoCtor(int x_) {
//    ~() {
//    }
//}

//-EXPECT-ERROR: takes no parameters
//CtorParam(int x_) {
//    _(int y) {
//    }
//    ~() {
//    }
//}

//-EXPECT-ERROR: takes no parameters
//DtorParam(int x_) {
//    _() {
//    }
//    ~(int y) {
//    }
//}

//-EXPECT-ERROR: Duplicate constructor
//DupCtor(int x_) {
//    _() {
//    }
//    _() {
//    }
//    ~() {
//    }
//}

//-EXPECT-ERROR: Duplicate destructor
//DupDtor(int x_) {
//    _() {
//    }
//    ~() {
//    }
//    ~() {
//    }
//}

//-EXPECT-ERROR: must be defined
//ForwardNoDef(int x_) {
//    _();
//    ~() { }
//}

//-EXPECT-ERROR: must be defined
//ForwardNoDef(int x_) {
//    _() { }
//    ~();
//}

/* a class name collides with a same-name FUNCTION (a class is not an overload). */
//-EXPECT-ERROR: Duplicate declaration of 'Clash'
//int Clash() { return 0; }
//Clash(int x_) { }

/* ...with an ALIAS. */
//-EXPECT-ERROR: Duplicate declaration of 'Clash'
//alias Clash = int;
//Clash(int x_) { }

/* ...with a file-scope CONST (the class is the source-later duplicate). */
//-EXPECT-ERROR: Duplicate declaration of 'Clash'
//const int Clash = 5;
//Clash(int x_) { }

/* a class field whose type names a non-type (a namespace) is rejected precisely. */
//-EXPECT-ERROR: is a namespace, not a type
//NsHolder { }
//FieldNs(NsHolder n_) { }
