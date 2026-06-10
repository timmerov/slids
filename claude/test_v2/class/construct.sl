/*
test class construction.

    Class (field-list) {body}

every field of a class is initialized before the constructor is called.
the destructor is called at end of scope.
the ctor and dtor are called exactly once each.
ctor/dtor are hooks executed at the start and end of the object's scope.
ctor/dtor have no author defined parameters.

a class conceptually desugars to a namespace and a named tuple.
like a namespace, you may declare and define things within the class body.
naked code in the class body is a compile error.
the field list is a data tuple where the slots are accessed by name.

ctor/dtor are optional.
they must appear together.
defining one without the other is a compile error.
the compiler will generate default (possibly) ctor/dtor if the author does not.

when a class is instantiated, the fields are initialized to:
1. the corresponding slot of an initialization tuple.
2. the default value, if any.
3. the appropriate zero value.
the default value for a field is zero unless otherwise defined by the author.


examples:

    Class(int f1_, int f2_) {
        _() {
            __println("Class:ctor");
        }
        ~() {
            __println("Class:dtor");
        }
    }

notes:

naming conventions are optional.
they are never used to resolve parse.

this file covers basic class features.
specialized class features are handled elsewhere.
*/

/*
claude says:

a class IS a named tuple. the kSlid type carries its own field-slot types, so
the whole tuple aggregate path (construct, store, slot access) is reused and
codegen needs no symbol table — the layout rides on the type. `.field` is just
slot access by name (lowered to a slot index in desugar).

construction is field-init (slot from an init tuple, else the author default,
else zero) — recursively for a class-typed field, where a scalar/tuple is the
sub-class's constructor input filled out with its own defaults. the `=` form
spreads its tuple across fields; the call form keeps each arg whole. a size-1
init tuple is inexpressible (`( x )` collapses) — punted to the call form.

ctor/dtor are NOT the constructor. fields are initialized first; the ctor is a
hook that runs after, the dtor a hook at scope exit. they are call-if-needed:
a trivial class emits no calls at all. forward declarations are allowed but
must be defined. the destructor-balance invariant — every instance destroyed
once, in reverse declaration order, on every exit (block end / return / break /
continue) — rides a scope chain threaded through codegen alongside the loop
context, gated by completion so we never emit after a terminator.
*/

MyFirstClass(
    int a_,
    int b_ = 1
) {
}

void print(MyFirstClass^ cls) {
    __println("MyFirstClass: a=" + cls^.a_ + " b=" + cls^.b_);
}

// gap 1: non-int field types. f_ has no default (zero 0.0); flag_/ch_ default.
Mixed(float f_, bool flag_ = true, char ch_ = 'Z') {
}

// gap 1: a pointer field defaults to nullptr (the zero pointer value).
Ptr(int^ p_) {
}

// gap 2: a narrow integer field; the default 5 fits int8.
Narrow(int8 small_ = 5) {
}

// gap 3: default variety — a negative default, a const-expression default, and
// defaults on NON-FINAL fields (neg_/base_ default; last_ does not).
const int BASE = 10;
Defs(int neg_ = -1, int base_ = BASE + 5, int last_) {
}

// gap 4: a class-typed field is recursively (default-)constructed.
Outer(MyFirstClass inner_) {
}

// gap 5: field counts other than two — one field, and three fields.
One(int x_ = 42) {
}
Three(int a_, int b_ = 2, int c_ = 3) {
}

TupleClass( (int,int) t_, (char,char,char) s_ ) {
}

void print(TupleClass^ tpl) {
    __println("TupleClass: "
        "t=(" + tpl^.t_[0] + "," + tpl^.t_[1] + ") " +
        "b=(" + tpl^.s_[0] + "," + tpl^.s_[1] + "," + tpl^.s_[2] + ")");
}

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

    MyFirstClass cls0;
    print(^cls0);

    MyFirstClass cls1();
    print(^cls1);

    MyFirstClass cls2(2);
    print(^cls2);

    MyFirstClass cls3 = 3;
    print(^cls3);

    MyFirstClass cls4(4,5);
    print(^cls4);

    MyFirstClass cls5 = (6, 7);
    print(^cls5);

    bool b = true;
    char a = 'a';
    MyFirstClass cls6 = (a, b);
    print(^cls6);

    int8 d = 42;
    MyFirstClass cls7 = (d);
    print(^cls7);

    TupleClass tpl = ((1,2), ('a','b','c'));
    print(^tpl);

    // gap 1 + gap 8: non-int fields, read directly off the value (no `^`).
    Mixed mx0;
    __println("mx0: f=" + mx0.f_ + " flag=" + mx0.flag_ + " ch=" + mx0.ch_);
    Mixed mx1(1.5, false, 'A');
    __println("mx1: f=" + mx1.f_ + " flag=" + mx1.flag_ + " ch=" + mx1.ch_);

    // gap 1: pointer field zero-constructs to nullptr.
    Ptr pz;
    if (pz.p_ == nullptr) {
        __println("pz.p_ is null");
    }

    // gap 2: int8 field from a fitting literal default / arg.
    Narrow nw0;
    __println("nw0: small=" + nw0.small_);
    Narrow nw1(7);
    __println("nw1: small=" + nw1.small_);

    // gap 3: default variety.
    Defs df0;
    __println("df0: neg=" + df0.neg_ + " base=" + df0.base_ + " last=" + df0.last_);
    Defs df1(7, 8, 9);
    __println("df1: neg=" + df1.neg_ + " base=" + df1.base_ + " last=" + df1.last_);

    // gap 4: nested class field, read through two field accesses.
    Outer ot;
    __println("ot.inner: a=" + ot.inner_.a_ + " b=" + ot.inner_.b_);

    /* pass the tuple to the field. */
    Outer ot2( (13,17) );
    print(^ot2.inner_);

    /*
    this is a vexing parse.
    there's no way to construct a size 1 tuple.
    punted for now.
    use the initializer list syntax.
    instead of the assign from tuple syntax.
    */
    /* pass the tuple to the field. */
    /*Outer ot3  = ( (13,17) );
    print(^ot3.inner_);*/

    /* pass the scalar to the field. */
    Outer ot4 = 19;
    print(^ot4.inner_);

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

    // gap 5: one-field and three-field classes.
    One one0;
    __println("one0: x=" + one0.x_);
    Three th0;
    __println("th0: a=" + th0.a_ + " b=" + th0.b_ + " c=" + th0.c_);
    Three th1(10, 20, 30);
    __println("th1: a=" + th1.a_ + " b=" + th1.b_ + " c=" + th1.c_);

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

//-EXPECT-ERROR: 2 field(s) but 3
//int32 neg_too_many() {
//    MyFirstClass bad(1, 2, 3);
//    print(^bad);
//    return 0;
//}

//-EXPECT-ERROR: has no field 'c_'
//int32 neg_no_field() {
//    MyFirstClass c(0);
//    int z = c.c_;
//    return z;
//}

//-EXPECT-ERROR: non-class value
//int32 neg_non_class() {
//    int n = 3;
//    int z = n.a_;
//    return z;
//}

//-EXPECT-ERROR: Duplicate field 'a_'
//Dup(int a_, int a_) {
//}

//-EXPECT-ERROR: Duplicate definition of class 'MyFirstClass'
//MyFirstClass(int x_) {
//}

//-EXPECT-ERROR: does not fit
//int32 neg_narrow() {
//    Narrow bad(300);
//    return bad.small_;
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

/* a class cannot contain itself by value — infinite size. (A reference '^' field
   breaks the cycle and is fine, as the forward-ref Now/Later above shows.) */
//-EXPECT-ERROR: contains itself by value
//SelfCycle(SelfCycle s_) { }

/* mutual by-value containment is infinite too (the cycle is transitive). */
//-EXPECT-ERROR: contains itself by value
//MutA(MutB b_) { }
//MutB(MutA a_) { }

/* the cycle is detected through an ARRAY field too. */
//-EXPECT-ERROR: contains itself by value
//SelfArr(SelfArr a_[2]) { }

/* ...and around a longer chain (A -> B -> C -> A). */
//-EXPECT-ERROR: contains itself by value
//CycA(CycB b_) { }
//CycB(CycC c_) { }
//CycC(CycA a_) { }

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
