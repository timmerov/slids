/*
test initializing classes.

    Class (field-list) {body}

every field of a class is initialized at the call site before the constructor is called.

when a class is instantiated, the fields are initialized to:
1. the corresponding slot of an initialization tuple.
2. the default value, if any.
3. the appropriate zero value.
the default value for a field is zero unless otherwise defined by the author.

class objects may be initialized from nothing, scalar, pointer, tuple/array, other class, literals,
variables, constants, function returns, and everything else that's tuple-like.
scalars and pointers are treated as tuples of size 1.
arrays are treated as homogeneous tuples.
classes are initialized from tuples (including scalars and arrays) by slot iteratively and recursively.
classes are initialized from other classes using the lhs class assignment operator.

initialization by tuple may be incomplete.
ie the class may have more fields than the tuple has slots.
remaining class fields are initialized to default values or zeros.

like local variables, initialization of a tuple or array field must be either default of complete.
it cannot be partial.

example declaration and initialization by tuple:

    Class(
        int f1_ = 1,
        int f2_ = 2,
        int f3_
    ) { }

    Class clsa;
    Class clsb(11);
    Class clsc(11,12);
    Class clsd(11,12,13);
    Class clse = 11;
    Class clsf = (11,12);
    Class clsg = (11,12,13);

    Super(
        Class c2_ = 21,
        Class c3_ = (31,32),
        Class c4_ = (41,42,43)
    } { }

    // defaults
    Super supa;

    // full
    Super supb((51,52,53), (54,55,54), (57,58,59));
    Super supc = ((51,52,53), (54,55,54), (57,58,59));

    // partial
    Super supd(61, (62,63));
    Super supe = (61, (62,63));
    Super supf(71, 72, 73);
    Super supg = (71, 72, 73);
    Super suph((81, 82, 83));

note: at this time it is not possible to declare a bare tuple of size 1.
however, the constructor-style syntax (see suph above) can parse a tuple of size 1.
use that syntax.
there are no plans to support tuples of size 1.
the declaration-assignment syntax (see supg above) likely will never work.

compile errors:

    // tuple too large.
    Class err1 = (1,2,3,4);

    // type mismatch
    Class err2 = 1.4;

notes:

naming conventions are optional.
they are never used to resolve parse.

this file covers initialization.
other class features are hendled elsewhere.
*/

/*
claude says:

a class IS a named tuple. the kSlid type carries its own field-slot types, so
the whole tuple aggregate path (construct, store, slot access) is reused and
codegen needs no symbol table — the layout rides on the type. `.field` is just
slot access by name (lowered to a slot index in desugar).

initialization is field-init by slot: each field takes the matching slot of the
init tuple, else the author default, else the type's zero value. a scalar or
pointer is a size-1 tuple; an array is a homogeneous tuple. the init may be
partial — fewer slots than fields — and the leftover fields fall through to
their default/zero. a class-typed field recurses: its slot (a scalar or tuple)
is that sub-class's own init, filled out with its own defaults; a same-type
class slot copies via the class assignment operator. the `=` form spreads its
tuple across fields; the call form keeps each arg whole. a size-1 init tuple is
inexpressible (`( x )` collapses) — punted to the call form.

field-list well-formedness is checked here too: duplicate field names are
rejected, and a class that contains itself by value (directly, mutually,
through an array, or around a longer cycle) is infinite-size and rejected — a
`^` reference field breaks the cycle.
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

// test 4 + 5: class-typed fields that carry defaults (a scalar default and a
// tuple default), more than one class field, and a class field with no default.
Super(
    MyFirstClass c2_ = 21,
    MyFirstClass c3_ = (31, 32),
    MyFirstClass c4_
) {
}

void print(Super^ s) {
    __println("Super: "
        "c2=(" + s^.c2_.a_ + "," + s^.c2_.b_ + ") " +
        "c3=(" + s^.c3_.a_ + "," + s^.c3_.b_ + ") " +
        "c4=(" + s^.c4_.a_ + "," + s^.c4_.b_ + ")");
}

// test 7: a `^` reference field may name the class's own type — the reference
// breaks the by-value cycle that would otherwise be infinite-size.
Link(int v_, Link^ next_) {
}

// 3x1 vs 1x3 tuple routing into class-typed fields. the `=` form spreads a
// 3-tuple across the three fields (each scalar recursively builds a field); the
// call form keeps a single 3-tuple arg whole, feeding the first 3-field field.
Trip(Three t1_, Three t2_, Three t3_) {
}

void print(Trip^ tr) {
    __println("Trip: "
        "t1=(" + tr^.t1_.a_ + "," + tr^.t1_.b_ + "," + tr^.t1_.c_ + ") " +
        "t2=(" + tr^.t2_.a_ + "," + tr^.t2_.b_ + "," + tr^.t2_.c_ + ") " +
        "t3=(" + tr^.t3_.a_ + "," + tr^.t3_.b_ + "," + tr^.t3_.c_ + ")");
}

// an array-typed field is filled by slot from its init tuple (homogeneous).
Arr(int xs_[3]) {
}

// an inline array of a class type — each element is recursively constructed.
Bag(Three items_[2]) {
}

void print(Bag^ bg) {
    __println("Bag: "
        "i0=(" + bg^.items_[0].a_ + "," + bg^.items_[0].b_ + "," + bg^.items_[0].c_ + ") " +
        "i1=(" + bg^.items_[1].a_ + "," + bg^.items_[1].b_ + "," + bg^.items_[1].c_ + ")");
}

// every field with no initializer gets its zero value — INCLUDING an array, a
// tuple, and a bool (each lacked a zero path before).
Zeros(int xs_[2], (bool, int) pair_, bool flag_) {
}

void print(Zeros^ z) {
    __println("Zeros: "
        "xs=" + z^.xs_[0] + "," + z^.xs_[1] + " " +
        "pair=" + z^.pair_[0] + "," + z^.pair_[1] + " " +
        "flag=" + z^.flag_);
}

// a function returning a tuple — an rvalue tuple-like source for class init.
(int,int,int) makeTriple() {
    (int,int,int) r = (40, 41, 42);
    return r;
}

// a function returning an array — another rvalue tuple-like source.
int[3] makeArr() {
    int v[3] = (70, 71, 72);
    return v;
}

// prints once per call — used to prove a side-effecting index in an aggregate
// source is evaluated ONCE (spilled to a temp), not once per field.
int pick() {
    __println("pick");
    return 1;
}

// a function returning a SHORTER tuple — an rvalue partial source.
(int,int) makePair() {
    (int,int) r = (50, 51);
    return r;
}

// a function returning a tuple of tuples — an rvalue source that recurses into
// class-typed fields.
((int,int,int),(int,int,int),(int,int,int)) makeNest() {
    ((int,int,int),(int,int,int),(int,int,int)) r = ((1,2,3), (4,5,6), (7,8,9));
    return r;
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

    // gap 5: one-field and three-field classes.
    One one0;
    __println("one0: x=" + one0.x_);
    Three th0;
    __println("th0: a=" + th0.a_ + " b=" + th0.b_ + " c=" + th0.c_);
    Three th1(10, 20, 30);
    __println("th1: a=" + th1.a_ + " b=" + th1.b_ + " c=" + th1.c_);

    // test 1: init a class from a pointer — a pointer is a size-1 tuple, filling
    // the (pointer) first field.
    int pv = 5;
    Ptr px = ^pv;
    __println("px.p_^ = " + px.p_^);

    /* test 3: init a class from another class via the lhs class assignment
       operator. deferred — not yet implemented; `= cls4` currently tries to feed
       the whole class into the first int field ("Cannot implicitly convert
       'MyFirstClass' to 'int'").
    MyFirstClass cp = cls4;
    print(^cp);
    // expect: MyFirstClass: a=4 b=5
    */

    // test 2: a class is initialized from any tuple-like VALUE — spread across
    // its fields by slot. an lvalue source (array / tuple variable) is indexed in
    // place; an rvalue source (function return) is materialized once.
    int arr[3] = (1, 2, 3);
    Three ta = arr;                 // array variable
    __println("ta: a=" + ta.a_ + " b=" + ta.b_ + " c=" + ta.c_);
    (int,int,int) tv = (4, 5, 6);
    Three tu = tv;                  // tuple variable
    __println("tu: a=" + tu.a_ + " b=" + tu.b_ + " c=" + tu.c_);
    Three tr = makeTriple();        // function return (rvalue, materialized once)
    __println("tr: a=" + tr.a_ + " b=" + tr.b_ + " c=" + tr.c_);
    int8 s8[3] = (9, 8, 7);
    Three tw = s8;                  // leaf-widen: int8 elements into int fields
    __println("tw: a=" + tw.a_ + " b=" + tw.b_ + " c=" + tw.c_);

    // more tuple-like sources: an op result, a sub-array row, a constant, and an
    // array-returning function.
    int oa[3] = (1, 2, 3);
    int ob[3] = (10, 20, 30);
    Three top = oa + ob;            // op result (rvalue) -> 11,22,33
    __println("top: a=" + top.a_ + " b=" + top.b_ + " c=" + top.c_);
    int grid[2][3] = ((1,2,3), (4,5,6));
    Three trow = grid[1];           // sub-array row (lvalue) -> 4,5,6
    __println("trow: a=" + trow.a_ + " b=" + trow.b_ + " c=" + trow.c_);

    // a side-effecting index in the source is evaluated ONCE (the source is spilled
    // to a temp), not once per field — "pick" prints exactly once.
    Three tse = grid[pick()];       // pick() -> row 1 = (4,5,6)
    __println("tse: a=" + tse.a_ + " b=" + tse.b_ + " c=" + tse.c_);
    const int cag[3] = (7, 8, 9);
    Three tcon = cag;               // constant source -> 7,8,9
    __println("tcon: a=" + tcon.a_ + " b=" + tcon.b_ + " c=" + tcon.c_);
    Three tret = makeArr();         // array-returning function (rvalue) -> 70,71,72
    __println("tret: a=" + tret.a_ + " b=" + tret.b_ + " c=" + tret.c_);

    // a PARTIAL aggregate source fills the leading fields; the rest take their
    // defaults / zeros (an array/tuple value spreads like a tuple).
    int pa[2] = (5, 6);
    Three tpart = pa;               // a_=5, b_=6, c_ defaults to 3
    __println("tpart: a=" + tpart.a_ + " b=" + tpart.b_ + " c=" + tpart.c_);

    // a NESTED aggregate source recurses into class-typed fields (each slot builds
    // a sub-class), exactly like the tuple-literal form.
    ((int,int,int),(int,int,int),(int,int,int)) tt = ((1,2,3), (4,5,6), (7,8,9));
    Trip trec = tt;
    print(^trec);

    // an RVALUE source behaves exactly like an lvalue: a function return is spilled
    // to a temp once, then spread — including partial fill and recursion.
    Three trvp = makePair();        // rvalue partial: a_=50, b_=51, c_ defaults to 3
    __println("trvp: a=" + trvp.a_ + " b=" + trvp.b_ + " c=" + trvp.c_);
    Trip trvr = makeNest();         // rvalue recursive into class-typed fields
    print(^trvr);

    // test 4 + 5: defaults, full recursive init, and partial recursive init of a
    // class with class-typed fields.
    Super sa;
    print(^sa);
    Super sb((51,52), (53,54), (55,56));
    print(^sb);
    Super sd(71);
    print(^sd);
    Super se(61, (62,63));
    print(^se);

    // test 7: a `^` self-reference field links to its own type (cycle broken).
    Link la(1, nullptr);
    Link lb(2, ^la);
    __println("lb.next_^.v_ = " + lb.next_^.v_);

    // 3x1: the `=` form spreads three scalars across the three class fields.
    Trip tg = (71, 72, 73);
    print(^tg);
    // 1x3: a single 3-tuple kept whole into the first 3-field class field.
    Trip th((81, 82, 83));
    print(^th);
    // contrast: the call form with three scalar args matches the 3x1 spread.
    Trip tf(71, 72, 73);
    print(^tf);

    // gap 1: the `=` form spreads tuple elements, each kept whole for a class
    // field (the canon's supc shape).
    Trip ts = ((1,2,3), (4,5,6), (7,8,9));
    print(^ts);

    // gap 2: an array-typed field filled by slot from its init tuple.
    Arr ar((10, 20, 30));
    __println("Arr: " + ar.xs_[0] + "," + ar.xs_[1] + "," + ar.xs_[2]);

    // gap 3: an inline array of a class type, each element constructed.
    Bag bg(( (1,2,3), (4,5,6) ));
    print(^bg);

    // gap 4: an array / tuple / bool field with no initializer zero-constructs.
    Zeros zr;
    print(^zr);

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

/* test 6: a type mismatch on init (a float value into an int field) is rejected. */
//-EXPECT-ERROR: Cannot implicitly convert 'float' to 'int'
//int32 neg_type_mismatch() {
//    MyFirstClass bad = 1.4;
//    print(^bad);
//    return 0;
//}

/* an aggregate-VALUE source with more slots than the class has fields is rejected
   (a class is a named tuple; the spread can't overfill). */
//-EXPECT-ERROR: 3 field(s) but 4
//int32 neg_agg_too_many() {
//    int big[4] = (1, 2, 3, 4);
//    Three t = big;
//    return t.a_;
//}

/* a per-leaf type mismatch from an aggregate source (a float element into an int
   field) is rejected, the same as a scalar init. */
//-EXPECT-ERROR: Cannot implicitly convert 'float' to 'int'
//int32 neg_agg_leaf_type() {
//    float fa[3] = (1.0, 2.0, 3.0);
//    Three t = fa;
//    return t.a_;
//}

/* a class cannot contain itself by value — infinite size. (A reference '^' field
   breaks the cycle and is fine, as the forward-ref Now/Later in construct.sl shows.) */
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
