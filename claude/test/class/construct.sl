/*
test class construction and destruction.

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

the primary object of this test file is to ensure that every possible
code path handles class construction and destruction properly.

things to test:
    ctor/dtor balance
    all code paths calls ctor/dtor
    array/tuple of class
    copy, move (in operator.sl)
    new/delete
    placement new/obj.~();
    loops, break, continue
    if, else
    return
    returned by function

deferred tests:
    temporary class objects in expressions and type conversions.

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

/* RAII: a class that frees a heap resource it owns in its destructor — `delete` of a
   FIELD (delete takes any pointer lvalue, not just a variable). */
Resource(int^ data_) {
    _() {
        __println("Resource:ctor");
    }
    ~() {
        __println("Resource:dtor");
        delete data_;
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

/*
destruction on a NON-block exit: a mid-function return tears down the locals
constructed so far, in reverse order, BEFORE control leaves. each path builds a
different second local, so only the actually-constructed instances are destroyed.
*/
int early_return(int v) {
    CtorDtor a(1);
    if (v > 0) {
        CtorDtor b(2);
        return 10;          /* tears down 2 then 1 */
    }
    CtorDtor c(3);
    return 20;              /* tears down 3 then 1 */
}

/* a hook local in a loop body is destroyed every iteration — including on the
   continue path and the break path. */
int loop_break_continue(int n) {
    int i = 0;
    while (i < n) {
        ++i;
        CtorDtor a(i);
        if (i == 2) { continue; }   /* dtor before continuing */
        if (i == 4) { break; }      /* dtor before breaking */
    }
    return 0;
}

/* conditional construction: only the taken branch's instance is built + torn down. */
int if_else(int v) {
    if (v > 0) {
        CtorDtor a(11);
    } else {
        CtorDtor b(12);
    }
    return 0;
}

/* returned by function (by value): the object is constructed and destructed
   exactly once (the by-value return is elided into a single instance). */
CtorDtor makeCtorDtor(int v) {
    CtorDtor local(v);
    return local;
}

/* a hook local in a FOR-RANGE body is destroyed every iteration. */
int for_range(int n) {
    for (i : 0..n) {
        CtorDtor a(i);
    }
    return 0;
}

/* a hook local in a DO-WHILE body is destroyed every iteration (incl. the first). */
int do_while(int n) {
    int i = 0;
    while {
        CtorDtor a(i);
        ++i;
    } (i < n);
    return 0;
}

/* a hook local in a SWITCH clause is destroyed at the clause's block scope. */
int switch_clause(int v) {
    switch (v) {
    1: { CtorDtor a(81); }
    2: { CtorDtor b(82); }
    }
    return 0;
}

/* a LABELED break tears down hook locals in BOTH the inner and outer loop scopes. */
int labeled_break(int n) {
    int i = 0;
    while (i < n) {
        CtorDtor a(91);
        ++i;
        int j = 0;
        while (j < n) {
            CtorDtor b(92);
            ++j;
            break outer;
        }
    } :outer;
    return 0;
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

    /* MULTI-DIM array of ctor classes — every element ctor'd in row-major order,
       dtor'd in reverse at scope exit. */
    {
        __println("ctors 41,42,43,44 after.");
        CtorDtor grid[2][2] = ((41, 42), (43, 44));
        __println("ctors 41,42,43,44 before dtors 44,43,42,41 after.");
    }
    __println("dtors 44,43,42,41 before.");

    /* tuple of ctor classes — each slot constructed by slot in order; dtors run in
       reverse slot order at scope exit. (The `(CtorDtor(14), ...)` temporary-in-
       expression spelling is still a front-end gap; this names the slot types.) */
    {
        __println("ctors 14,24,34 after.");
        (CtorDtor, CtorDtor, CtorDtor) t = (14, 24, 34);
        __println("ctors 14,24,34 before dtors 34,24,14 after.");
    }
    __println("dtors 34,24,14 before.");

    {
        Now now(97);
    }

    /* destruction on a mid-function return (partial construction per path). */
    __println("early_return(1): expect ctors 1,2 then dtors 2,1.");
    early_return(1);
    __println("early_return(0): expect ctors 1,3 then dtors 3,1.");
    early_return(0);

    /* a hook local in a loop, destroyed each iteration incl. continue + break. */
    __println("loop_break_continue(5): expect ctor/dtor 1,2,3,4 (break at 4).");
    loop_break_continue(5);

    /* conditional construction — only the taken branch builds + tears down. */
    __println("if_else(1): expect ctor/dtor 11.");
    if_else(1);
    __println("if_else(0): expect ctor/dtor 12.");
    if_else(0);

    /* returned by function, by value: one ctor, one dtor (result discarded). */
    __println("makeCtorDtor(7): expect ctor/dtor 7.");
    makeCtorDtor(7);

    /* a hook local in a FOR-RANGE body — ctor/dtor each iteration. */
    __println("for_range(3): expect ctor/dtor 0,1,2.");
    for_range(3);

    /* a hook local in a DO-WHILE body — ctor/dtor each iteration (incl. first). */
    __println("do_while(3): expect ctor/dtor 0,1,2.");
    do_while(3);

    /* a hook local in a SWITCH clause — ctor/dtor at the clause block scope. */
    __println("switch_clause(1): expect ctor/dtor 81.");
    switch_clause(1);

    /* a LABELED break — tears down hook locals in inner AND outer loop scopes. */
    __println("labeled_break(2): expect ctors 91,92 then dtors 92,91.");
    labeled_break(2);

    /* heap: the dtor runs on delete. */
    {
        __println("new/delete: expect ctor/dtor 50.");
        CtorDtor^ p = new CtorDtor(50);
        delete p;
    }

    /* RAII: a class's dtor frees a heap field it owns (delete of a field). */
    {
        __println("RAII: expect Resource ctor then dtor.");
        int^ d = new int;
        Resource r(d);
    }

    /* heap ARRAY of ctor class — `new T[n]` default-constructs each element (the
       count rides an 8-byte cookie), `delete` destructs each in reverse then frees.
       Elements are default-constructed, so each c_ is 0. Note the iterator type
       `CtorDtor[]` (NOT `CtorDtor^`): a single-ref + single-delete of an array
       allocation mismatches the cookie. */
    {
        __println("new[]/delete: expect ctors 0,0,0 then dtors 0,0,0.");
        CtorDtor[] pa = new CtorDtor[3];
        delete pa;
    }

    /* placement new into a raw buffer + explicit obj^.~(); the buffer is freed
       with no automatic dtor, so the object is destroyed exactly once. */
    {
        __println("placement new + obj^.~(): expect ctor/dtor 60.");
        int8[] raw = new int8[sizeof(CtorDtor)];
        CtorDtor^ pp = new(raw) CtorDtor(60);
        pp^.~();
        delete raw;
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

/* an explicit '.~()' on an automatically-managed (non-placement) object is
   rejected: the scope-end dtor already runs, so it would double-destruct. The
   '.~()' form is only for placement-constructed objects reached via a pointer. */
//-EXPECT-ERROR: only allowed on a placement-constructed object
//int neg_explicit_dtor_auto(int v) {
//    CtorDtor x(1);
//    x.~();
//    return 0;
//}
