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

tbd
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

/*
CtorDtor(int c_) {
    _() {
        __println("CtorDtor:ctor");
    }
    ~() {
        __println("CtorDtor:dtor");
    }
}
*/

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

    // gap 5: one-field and three-field classes.
    One one0;
    __println("one0: x=" + one0.x_);
    Three th0;
    __println("th0: a=" + th0.a_ + " b=" + th0.b_ + " c=" + th0.c_);
    Three th1(10, 20, 30);
    __println("th1: a=" + th1.a_ + " b=" + th1.b_ + " c=" + th1.c_);

    /*
    {
        __println("expect 3 ctors below.");
        CtorDtor cd1;
        CtorDtor cd2;
        CtorDtor cd3;
        __println("expect 3 ctors above and 3 dtors below.");
    }
    __println("expect 3 dtors above.");
    */

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
