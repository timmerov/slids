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

there are two syntactical shapes for object instantiation:
instantiation by 1) tuple or 2) assignment.
the two shapres are usually interchangeable.

    Class name ( initializer ) ;
    Class name = initializer ;

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

/* the two instantiation shapes are NOT interchangeable when the class defines a tuple
   op=: `X x(a,b)` field-list-constructs in place (ctor only), while `X x = (a,b)` is
   value-init — default-construct then dispatch op=((int,int)^). */
TupleInit(int p_, int q_) {
    _() { __println("TupleInit:ctor: (" + p_ + "," + q_ + ")"); }
    ~() { __println("TupleInit:dtor: (" + p_ + "," + q_ + ")"); }
    op=( (int, int)^ t ) {
        p_ = t^[0];
        q_ = t^[1];
        __println("TupleInit:op=tuple: (" + p_ + "," + q_ + ")");
    }
}

/* a class with a SCALAR op= — for the scalar-value explode and the construction-vs-value
   collapse guard: `ScalarOp(5)` (construction) must field-list, while `= 5` must op=. */
ScalarOp(int v_) {
    _() { __println("ScalarOp:ctor: " + v_); }
    ~() { __println("ScalarOp:dtor: " + v_); }
    op=(int x) { v_ = x; __println("ScalarOp:op=int: " + v_); }
}

/* a class with a class-typed FIELD. In the CONSTRUCTION spelling (`Boxed bx(7,(1,2))`) the
   field FIELD-LISTS (one ctor, no op=). The VALUE-INIT spelling (`Boxed bx = (7,(1,2))`) and a
   field COPY (`Boxed bx(7, t)`) would each need the field to dispatch its op= / copy, which a
   field's transfer cannot hoist past the enclosing ctor to do (splitTransferInit's field note /
   todo) — so both are rejected rather than silently field-listed or blitted (negatives below). */
Boxed(int tag_, TupleInit inner_) {
    _() { __println("Boxed:ctor: " + tag_); }
    ~() { __println("Boxed:dtor: " + tag_); }
}

/* TRIVIAL (POD) classes: no ctor/dtor. A trivial class-typed FIELD is exempt from the
   field-transfer rejections a hook-bearing one triggers — a copy from an lvalue, a move from a
   call rvalue, and a value-init all stay legal, because the blit is byte-for-byte correct with
   no op= / ctor to skip or observe. */
Pod(int a_, int b_, int c_) { }
SuperPod(Pod p_, int d_) { }
Pod makePod() { return Pod(1, 2, 3); }

// All fields have AUTHOR DEFAULTS, so an EMPTY init slot (`Defs d(,2,3)`) is observable —
// the defaulted position reads its default (7/8/9), distinct from a provided value.
Defs(int a_ = 7, int b_ = 8, int c_ = 9) { }

Aop(int v_) { _(){} ~(){} op=(int x) { v_ = x; } }
Bw(Aop a_) { }
Cw(Bw b_) { }
Dw(Cw c_) { }
Multi(int x_, Aop a_, int y_) { }
Bw : HasBaseWithOp(int own_) { }   // base Bw (contains Aop) is slot 0

/* the trivial-BUCKET transfer classes: a user op= / op<-- but NO ctor and NO dtor. Every
   field negative above uses a field class WITH a ctor/dtor (TupleInit, Aop, Bw-contains-Aop),
   so it rejects through the `needs_ctor || needs_dtor` gate. These two reject ONLY through the
   direct operator scan (userSelfTransferOpId) — the gate and findClassOperator both miss a
   trivial-bucket class's self-op. Delete that scan and the two negatives below go GREEN (a
   silent blit past the operator), which is the regression they guard. */
CopyOnly(int v_) { op=(CopyOnly^ r) { v_ = r^.v_; } }           // op=-only, no _()/~()
MoveOnly(int v_) { op<--(mutable MoveOnly^ r) { v_ = r^.v_; } }  // op<--only, no _()/~()
HoldCopy(CopyOnly a_) { }
HoldMove(MoveOnly a_) { }
MoveOnly mkMoveOnly() { return MoveOnly(1); }

/* the RETURN slot is the declarator funnel's twin: a single class returned from a VALUE
   binds `_$ret` and op='s / field-lists in place (NRVO builds it in the caller's slot). */
TupleInit retTuple()  { return (1, 2); }   /* value op=((int,int)^) */
ScalarOp   retScalar() { return 7; }        /* value op=(int) */

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

    /* construction shape vs assignment shape diverge once a tuple op= exists:
       `(a,b)` builds in place (one ctor); `= (a,b)` default-constructs then op='s. */
    {
        __println("TupleInit tp(7,8): expect ctor (7,8) only.");
        TupleInit tp(7, 8);
        __println("TupleInit ta = (5,6): expect ctor (0,0) then op=tuple (5,6).");
        TupleInit ta = (5, 6);
        __println("expect dtors (5,6) then (7,8) after.");
    }
    __println("dtors (5,6),(7,8) before.");

    /* THE SLOT-WISE EXPLODE for op=: a TUPLE of classes initialized from a tuple-of-tuples
       does NOT field-list each slot — every slot whose value matches a tuple op= is
       default-constructed in place, then op='d in place (no temp, no copy). The decl
       default-constructs both slots FIRST, then the peeled op= statements run in slot
       order. */
    {
        __println("(TupleInit,TupleInit) pair = ((1,2),(3,4)):");
        __println("  expect ctors (0,0),(0,0) then op=tuple (1,2),(3,4).");
        (TupleInit, TupleInit) pair = ((1, 2), (3, 4));
        __println("  expect dtors (3,4),(1,2) after.");
    }
    __println("pair dtors before.");

    /* the same explode reaches ARRAY elements: each element op='s in place. */
    {
        __println("TupleInit row[2] = ((5,6),(7,8)):");
        __println("  expect ctors (0,0),(0,0) then op=tuple (5,6),(7,8).");
        TupleInit row[2] = ((5, 6), (7, 8));
        __println("  expect dtors (7,8),(5,6) after.");
    }
    __println("row dtors before.");

    /* a MIXED aggregate: a field-list slot (no matching op=) still builds in place while
       its sibling op='s — the explode is per slot. TupleInit has a 2-int op= but CtorDtor
       does not, so the CtorDtor slot field-lists (one ctor) and the TupleInit slot op='s. */
    {
        __println("(CtorDtor,TupleInit) mix = (9,(1,2)):");
        __println("  expect CtorDtor ctor 9, TupleInit ctor (0,0) then op=tuple (1,2).");
        (CtorDtor, TupleInit) mix = (9, (1, 2));
        __println("  expect dtors TupleInit (1,2), CtorDtor 9 after.");
    }
    __println("mix dtors before.");

    /* the explode is recursive: a NESTED aggregate op='s every leaf slot in place. */
    {
        __println("((TupleInit,TupleInit),TupleInit) nest = (((1,2),(3,4)),(5,6)):");
        __println("  expect 3 ctors (0,0) then op=tuple (1,2),(3,4),(5,6).");
        ((TupleInit, TupleInit), TupleInit) nest = (((1, 2), (3, 4)), (5, 6));
        __println("  expect dtors (5,6),(3,4),(1,2) after.");
    }
    __println("nest dtors before.");

    /* a SCALAR-valued op= per slot (not just tuple values): ScalarOp defines op=(int). */
    {
        __println("(ScalarOp,ScalarOp) sc = (11,22):");
        __println("  expect ctors 0,0 then op=int 11,22.");
        (ScalarOp, ScalarOp) sc = (11, 22);
        __println("  expect dtors 22,11 after.");
    }
    __println("sc dtors before.");

    /* a same-class LVALUE slot is a COPY, not an op=: buildClassFromValue excludes a value
       already of the class type, so the slot is default-constructed then whole-value copied
       (the synthesized copy is silent — only the two default ctors and the source ctors print). */
    {
        __println("(TupleInit,TupleInit) cp = (a,b) [lvalues]:");
        __println("  expect ctors a(1,2),b(3,4), then two default ctors (0,0) (copies silent).");
        TupleInit a(1, 2);
        TupleInit b(3, 4);
        (TupleInit, TupleInit) cp = (a, b);
        __println("  cp[0]=(" + cp[0].p_ + "," + cp[0].q_ + ") cp[1]=(" + cp[1].p_ + "," + cp[1].q_ + ")");
    }
    __println("cp/a/b dtors before.");

    /* a class-typed FIELD in the CONSTRUCTION spelling FIELD-LISTS (one ctor, no op=) — fields
       stay field-list even when the field class has a matching op=. The VALUE-INIT spelling and
       a field COPY are rejected instead (negatives at end). */
    {
        __println("Boxed bx(7,(1,2)):");
        __println("  expect TupleInit ctor (1,2) [field-list, no op=], then Boxed ctor 7.");
        Boxed bx(7, (1, 2));
        __println("  bx.inner=(" + bx.inner_.p_ + "," + bx.inner_.q_ + ")");
    }
    __println("bx dtors before.");

    /* a TRIVIAL (no ctor/dtor) class FIELD is exempt from the rejections: a copy from an lvalue,
       a move from a call rvalue, and a value-init are all legal — the blit is byte-correct with
       nothing to skip. No lifecycle output (Pod/SuperPod are silent); the field values prove it. */
    {
        __println("SuperPod (trivial Pod field):");
        SuperPod sa(1, 2);
        __println("  sa(1,2):                p_=(" + sa.p_.a_ + "," + sa.p_.b_ + "," + sa.p_.c_ + ") d_=" + sa.d_);
        SuperPod sb((12, 3), 4);
        __println("  sb((12,3),4):           p_=(" + sb.p_.a_ + "," + sb.p_.b_ + "," + sb.p_.c_ + ") d_=" + sb.d_);
        Pod p(1, 2, 3);
        SuperPod sc(p, 4);
        __println("  sc(p,4) [POD copy]:     p_=(" + sc.p_.a_ + "," + sc.p_.b_ + "," + sc.p_.c_ + ") d_=" + sc.d_);
        SuperPod sd(makePod(), 4);
        __println("  sd(makePod(),4) [move]: p_=(" + sd.p_.a_ + "," + sd.p_.b_ + "," + sd.p_.c_ + ") d_=" + sd.d_);
        SuperPod se = (makePod(), 4);
        __println("  se=(makePod(),4)[vinit]: p_=(" + se.p_.a_ + "," + se.p_.b_ + "," + se.p_.c_ + ") d_=" + se.d_);
    }

    /* EMPTY INIT SLOTS in a construction field-list (`Class c(,2,3)` / `(1,,3)`): a LEADING or
       INTERIOR empty slot takes the field's AUTHOR DEFAULT (Defs: 7/8/9), or ZERO for a field
       with no default (Pod). It CONSUMES its flat position, so the values after it still align.
       Construction form only; a trailing comma is a negative below. */
    {
        __println("empty init slots:");
        Defs da(, 2, 3);      // a_ defaults to 7
        __println("  Defs da(,2,3): (" + da.a_ + "," + da.b_ + "," + da.c_ + ")");   // (7,2,3)
        Defs db(1, , 3);      // b_ defaults to 8
        __println("  Defs db(1,,3): (" + db.a_ + "," + db.b_ + "," + db.c_ + ")");   // (1,8,3)
        Defs dc(, , 3);       // a_ and b_ default
        __println("  Defs dc(,,3):  (" + dc.a_ + "," + dc.b_ + "," + dc.c_ + ")");   // (7,8,3)
        Pod pe(, 5, 6);       // no default -> a_ zero-inits
        __println("  Pod pe(,5,6):  (" + pe.a_ + "," + pe.b_ + "," + pe.c_ + ")");   // (0,5,6)
    }

    /* the RETURN slot funnels a value the same way — op= (tuple) and op= (scalar), NRVO'd
       into the caller's slot (one object each). */
    {
        __println("TupleInit t = retTuple(): expect ctor (0,0) then op=tuple (1,2).");
        TupleInit t = retTuple();
        __println("  t=(" + t.p_ + "," + t.q_ + ")");
    }
    {
        __println("ScalarOp s = retScalar(): expect ctor 0 then op=int 7.");
        ScalarOp s = retScalar();
        __println("  s=" + s.v_);
    }

    /* MOVE-init of an op= aggregate explodes per slot exactly like copy-init. */
    {
        __println("(TupleInit,TupleInit) mv <-- ((1,2),(3,4)):");
        __println("  expect ctors 0,0 then op=tuple (1,2),(3,4).");
        (TupleInit, TupleInit) mv <-- ((1, 2), (3, 4));
        __println("  expect dtors (3,4),(1,2) after.");
    }
    __println("mv dtors before.");

    /* construction-vs-value collapse guard: `ScalarOp(5)` is a BUILD (field-list ctor),
       NOT a value op=(int) — the `(5)` arg tuple must not collapse into the op=(int) source.
       Its `= 5` sibling IS a value op=. */
    {
        __println("ScalarOp cx = ScalarOp(5): expect ctor 5 ONLY (build, no op=int).");
        ScalarOp cx = ScalarOp(5);
        __println("ScalarOp cy = 5: expect ctor 0 then op=int 5.");
        ScalarOp cy = 5;
    }

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

/* a class-from-VALUE whose tuple matches NO op= falls back to field-list, which enforces
   arity: TupleInit has 2 fields and its only op= takes a 2-tuple, so a 3-tuple is neither a
   value op= nor a valid field list. (The op= funnel does not paper over a size mismatch.) */
//-EXPECT-ERROR: has 2 field(s) but 3 initializer(s)
//int neg_class_from_tuple_arity() {
//    TupleInit t = (1, 2, 3);
//    return t.p_;
//}

/* a class-typed FIELD in the VALUE-INIT spelling would need the field to dispatch its op=
   (default-construct, then assign) — a transfer that cannot hoist past the enclosing ctor. It
   is rejected rather than silently field-listed. (The CONSTRUCTION spelling `Boxed b(7,(1,2))`
   is legal and field-lists — a positive above.) */
//-EXPECT-ERROR: cannot dispatch 'TupleInit.op='
//int neg_field_value_init_op() {
//    Boxed b = (7, (1, 2));
//    return b.inner_.p_;
//}

/* copying a class VALUE into a class FIELD would need the field's op=(Class^) copy under the
   copy-into order (the field default-constructed first, its ctor observing the default). The
   copy cannot hoist between the field's ctor and the enclosing ctor body, so it is rejected
   rather than blitted-then-constructed-over. (A trivial field class, with nothing observable to
   skip, is unaffected.) */
//-EXPECT-ERROR: cannot be initialized by copying
//int neg_field_copy() {
//    TupleInit t(1, 2);
//    Boxed b(9, t);
//    return b.inner_.p_;
//}

/* the same restriction reaches a same-class RVALUE: a call result (or any non-construction
   rvalue) MOVES into the field (op<--), which likewise cannot hoist into the field's own
   construction, so codegen would blit a throwaway temp in and construct over it — rejected too.
   Only an in-place construction ('TupleInit(...)') is a legal class-field value. */
//-EXPECT-ERROR: cannot be initialized by moving
//int neg_field_move() {
//    Boxed b(9, retTuple());
//    return b.inner_.p_;
//}

/* the value-init rejection recurses: Dw -> Cw -> Bw -> Aop, so the value threaded to the
   DEEPLY nested Aop field (which has op=(int)) is caught at depth. */
//-EXPECT-ERROR: cannot dispatch 'Aop.op='
//int neg_field_deep() {
//    Dw dw = 5;
//    return dw.c_.b_.a_.v_;
//}

/* the rejected field need not be the FIRST: Multi's op= field a_ is the second, reached by the
   flat init index. */
//-EXPECT-ERROR: cannot dispatch 'Aop.op='
//int neg_field_not_first() {
//    Multi m = (1, 5, 9);
//    return m.a_.v_;
//}

/* the rejection reaches a field inside a BASE subobject: HasBaseWithOp's base Bw carries the
   Aop field, and value-init flows through the base flat-splice. */
//-EXPECT-ERROR: cannot dispatch 'Aop.op='
//int neg_field_through_base() {
//    HasBaseWithOp h = (5, 9);
//    return h.own_;
//}

/* the copy rejection covers a BASE subobject given a WHOLE same-base value (not the flat scalar
   splice): the base is a subobject like any field, and a blit past its transfer is the same bug. */
//-EXPECT-ERROR: Base 'Bw' of 'HasBaseWithOp' cannot be initialized by copying
//int neg_base_copy() {
//    Bw b(Aop(1));
//    HasBaseWithOp h(b, 9);
//    return h.own_;
//}

/* THE CLOSED GAP — COPY: copying an op=-only class (no ctor/dtor) into a field. The
   `needs_ctor/needs_dtor` gate does not fire (nothing in the trivial bucket), so only the
   direct op= scan rejects the blit that would skip CopyOnly.op=. */
//-EXPECT-ERROR: cannot be initialized by copying
//int neg_field_copy_oponly() {
//    CopyOnly c(1);
//    HoldCopy h(c);
//    return h.a_.v_;
//}

/* THE CLOSED GAP — MOVE: moving an op<--only class (no ctor/dtor) rvalue into a field. Same
   trivial-bucket blind spot; the direct op<-- scan rejects the blit that would skip
   MoveOnly.op<--. */
//-EXPECT-ERROR: cannot be initialized by moving
//int neg_field_move_oponly() {
//    HoldMove h(mkMoveOnly());
//    return h.a_.v_;
//}

/* EMPTY INIT SLOTS reject a TRAILING comma — a leading/interior empty slot means "default this
   field", but a comma with nothing after it names no field to default; it is a typo, not a slot. */
//-EXPECT-ERROR: a trailing comma is not an empty slot
//int neg_trailing_comma() {
//    Defs d(1, 2, );
//    return d.a_;
//}
