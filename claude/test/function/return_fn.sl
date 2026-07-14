/*
test returning non-primitive values -
arrays, tuples, classes.

the function signature desugars unambiguously:

    Type fn();

desugars to:

    void fn_$desugar(Type^ ret);

the function has three options for returning the value.

case 1: return an unnamed variable.
the return value is constructed in place.

    Type fn() {
        return Type;
    }

desugars to:

    void fn_$desugar(Type^ ret) {
        /* if class-like object */
        initialize ret^;
        ret^.ctor();
        /* ret^ dtor is never called in the function. */
    }

case 2: an unambiguous named variable.
again the return value is constructed in place.

    Type fn() {
        Type obj;
        return obj;
    }

desugars to:

    void fn_$desugar(Type^ ret) {
        alias obj = ret^;
        /* if class-like object */
        initialize obj;
        obj.ctor();
        /* obj dtor is never called in the function. */
    }

case 3 (the fallback): multiple overlapping return paths.
the return slot is constructed.
then the value to be returned is moved (move semantics) to the return slot.

    Type fn() {
        Type a;
        Type b;
        if (cond) {
            return a;
        else {
            return b;
        }
    }

desugars to:

    void fn_$desugar(Type^ ret) {
        Type a;
        Type b;
        if (cond) {
            /* if class-like object */
            /* use move-init semantics if possible */
            initialize ret^;
            ret^.ctor();
            ret^ <-- a;
            /* ret^ dtor is never called in the function. */
            return;
        else {
            /* if class-like object */
            /* use move-init semantics if possible */
            initialize ret^;
            ret^.ctor();
            ret^ <-- b;
            /* ret^ dtor is never called in the function. */
            return;
        }
    }

the call site has four options.

if the return type of the function and the type of the lhs match exactly,
and either there is no ctor at any leaf, or the lhs is being created,
then the call can be completed in place.

case 1: types match exactly and the lhs is being created here:
elision.

    Type obj = fn();
    Type obj <-- fn();

desugars to:

    allocate space for obj.
    fn_$desugar(^obj);

case 2: types match exactly and the lhs is plain old data:
to be plain old data, no leaf of the type may contain a class with a ctor
or the applicable transfer operator (assign or move).
elision.

    Type obj;
    obj = fn();
    obj <-- fn();

desugars to:

    Type obj;
    fn_$desugar(^obj);

case 3: types match exactly and not case 1 or 2.
elision is not possible.
spill to a temp and copy/move it to the target.

    Type obj_copy;
    Type obj_move;
    obj_copy = fn();
    obj_move <-- fn();

desugars to:

    Type obj_copy;
    Type obj_move;
    {
        alloca $temp;
        fn_$desugar(^$temp);
        obj_copy = $temp;
        $temp.dtor();
    }
    {
        alloca $temp;
        fn_$desugar(^$temp);
        obj_move <-- $temp;
        $temp.dtor();
    }

case 4: types do not match exactly.
elision is not possible.
spill to a temp and assign/move it to the target.

    OtherType obj_assign;
    OtherType obj_move;
    obj_assign = fn();
    obj_move <-- fn();

desugars to:

    OtherType obj_assign;
    OtherType obj_move;
    {
        alloca $temp;
        fn_$desugar(^$temp);
        obj_assign = $temp;
        $temp.dtor();
    }
    {
        alloca $temp;
        fn_$desugar(^$temp);
        obj_move <-- $temp;
        $temp.dtor();
    }

note:
in the desugaring blocks of cases 3,4 when the temp is transfered to the target...
for case 3:
'=' means use the assignment operator op=.
'<--' means use the move operator op<--.
the compiler synthesizes these if they're are not explicitly defined.
for case 4:
'=' means use a matching assignment operator op=.
'<--' means use a matching move operator op<--.
it's a compiler error if there is no matching operator.

in all cases, the desugared function is the same.
the only difference is how the call site handles the returned value.

such a function used in an expression is writing to a newly created temporary variable.
it's equivalent to call site case 1.

notes:

nit: the types need to match almost-exactly.
for example: (int,int) and int[2] share identical layout.
so they could take the in-place route.
aspirational feature.

signatures to test with both variable declaration and existing variable.

    int[3] fn();
    (int,int) fn();
    (int[3], int[3]) fn();
    (int, int)[3] fn();
    Class fn();
    Class[3] fn();
    (Class, Class) fn();
*/

/*
claude says:

always-sret + ELIDE-WHENEVER-POSSIBLE on both sides.

A function returning a non-primitive type (array / tuple / class, or an aggregate
with a class leaf) is ALWAYS desugared to `void fn_$desugar(Type^ ret)`. The body
writes the result into `ret^` and never destructs it (the caller owns the dtor).
  - FUNCTION: NRVO builds a returned local straight into `ret^` when its lifetime is
    disjoint (one ctor, no move); overlapping returns fall back to `ret^ <-- local`.
  - CALLER: the elide-or-spill decision funnels through the decl-init binding path.
    A FRESH decl of the exact type from a call/construction rvalue ELIDES — the call
    builds straight into the target's storage (header cases 1). Elision is skipped
    only when it CAN'T apply: an existing var (already constructed → op= / op<-- or a
    move-assign, cases 2-4), or a non-exact / lvalue source. An inline use materializes
    a temp (caller case 1 into a temporary).

The invariant: ctor count == dtor count. The classes below print id on ctor/dtor so the
balance is visible in the golden; Op additionally prints copy/move so a FIRED operator
(existing-var only) is distinct from an elided decl-init.
*/

// A hook class — prints id on ctor + dtor so ctor/dtor balance is visible.
Class(int id_) {
    _() { __println("ctor " + id_); }
    ~() { __println("dtor " + id_); }
}

// POD aggregate returns (no leaf ctor) — sret mechanism, no move/dtor balance.
int[3] mkArr() {
    int a[3] = (1, 2, 3);
    return a;
}
(int, int) mkTup() {
    return (4, 5);
}

// hook-class returns — the balance focus.
Class mkClass() {
    Class c(7);
    return c;                 // function fallback: ret^ default-move-init <-- c
}
(Class, Class) mkPair() {
    Class a(8);
    Class b(9);
    return (a, b);
}
int idOf(Class^ p) { return p^.id_; }
Class wrapClass() { return mkClass(); }   // return-of-call: forwards the slot
Class bump(Class^ p) { Class r(p^.id_ + 1); return r; }  // for a nested inline call

// a class with a POINTER leaf — exercises the construct path past a non-POD field.
Holder(int^ p_) { _() { __println("hctor"); } ~() { __println("hdtor"); } }
Holder mkHolder(int^ q) { Holder h(q); return h; }

// aggregate returns with a class leaf: array-of-class, mixed, and nested.
Class[3] mkArrC() { Class a[3] = (10, 20, 30); return a; }
(Class, int) mkMixed() { Class c(5); return (c, 9); }   // rvalue tuple -> fallback
(Class[2], int) mkNested() { Class a[2] = (6, 7); return (a, 8); }

// POD aggregate returns the spec lists (nested + array-of-tuples), and a NON-EXACT
// leaf-widen return (int8 tuple -> int tuple) that takes the convert fallback.
(int[3], int[3]) mkPodNest() { (int[3], int[3]) v = ((1,2,3),(4,5,6)); return v; }
(int, int)[2] mkPodAoT() { (int, int) v[2] = ((1,2),(3,4)); return v; }
(int, int) mkWiden() { (int8, int8) v = (1, 2); return v; }

// returning a tuple LITERAL as an array (rvalue return — reaches emitExpr, which
// builds an [N x T] value), and a helper that takes a call result by reference.
int[3] retLit() { return (1, 2, 3); }
int sumPair((int, int)^ p) { return p^[0] + p^[1]; }

// NRVO variants: the SAME local returned from two paths (NRVO applies); a disjoint
// multi-local return (good — different local per path, never coexisting → each NRVOs
// into the shared slot); and an overlapping multi-local return (bad — both locals
// alive at once → falls back to a move).
Class condSame(bool b) { Class c(3); if (b) { return c; } return c; }
Class pickGood(bool b) { if (b) { Class p(1); return p; } Class q(2); return q; }
Class pickBad(bool b) { Class a(1); Class b2(2); if (b) { return a; } return b2; }
// if/else disjoint (the canonical good()) — both locals NRVO into the shared slot.
Class pickGood2(bool b) { if (b) { Class p(1); return p; } else { Class q(2); return q; } }
// overlap via an OUTER local alive during the inner-else local — both fall back.
Class pickBad2(bool b) { Class a(1); if (b) { return a; } else { Class b2(2); return b2; } }
// CALL-INIT local that is NRVO-returned: x is both call-initialized (sret build-in-place —
// mkClass builds straight into x's storage) AND nrvo (single disjoint return), so x aliases
// the CALLER's slot and mkClass builds all the way through it — ONE ctor, no move, and the
// CALLER owns the SINGLE dtor. This frame must NOT register x for destruction (it doesn't
// own it); a regression that did would dtor the object twice.
Class mkFromCall() { Class x = mkClass(); return x; }

// ELIDE-WHENEVER-POSSIBLE (header canon above): a class that DEFINES op= / op<-- still
// ELIDES a class rvalue into a FRESH decl (`Op x = mkOp()` / `Op x <-- mkOp()`) — the
// operator does NOT run, even though `=` / `<--` syntax is used. The operator fires only
// on an EXISTING variable (the author forces it by declaring, then assigning). Op prints
// ctor/dtor plus copy/move (so a fired operator is visible); op= adds 100, op<-- adds
// 200 — so an ELIDED decl stays 7, and an existing-var assign turns 7 into 107 / 207.
Op(int v_) {
    _()                    { __println("ctor " + v_); }
    ~()                    { __println("dtor " + v_); }
    op=(Op^ rhs)           { v_ = rhs^.v_ + 100; __println("copy"); }
    op<--(mutable Op^ rhs) { v_ = rhs^.v_ + 200; rhs^.v_ = 0; __println("move"); }
}
Op mkOp() { Op o(7); return o; }

// THE RETURN SLOT IS CONSTRUCTED, THEN TRANSFERRED INTO (header case 3: `initialize ret^;
// ret^.ctor(); ret^ <-- a;`). Every class above only PRINTS in its ctor, so a slot FILLED and
// then CONSTRUCTED shows a wrong ORDER but never a wrong ANSWER. Ret's ctor WRITES its own
// field — so if the ctor runs on top of the transferred-in value, the caller reads 99.
Ret(int v_) {
    _() { v_ = 99; }
    ~() { }
}
// a TUPLE of class lvalues: the fallback, per slot.
(Ret, Ret) retPair() {
    Ret a(0); a.v_ = 1;
    Ret b(0); b.v_ = 2;
    return (a, b);
}
// a MIXED literal: slot 0 is transferred in (1), slot 1 is BUILT in its slot, so its ctor
// sees its field initializer and overwrites it (99) — a construction keeps its own order.
(Ret, Ret) retMixed() {
    Ret a(0); a.v_ = 3;
    return (a, Ret(0));
}
// the SCALAR fallback — overlapping locals, so neither NRVOs.
Ret retPick(bool b) {
    Ret a(0); a.v_ = 4;
    Ret c(0); c.v_ = 5;
    if (b) { return a; }
    return c;
}
// a PARAM is an lvalue with no local storage to alias — always the fallback.
Ret retParam(Ret^ p) { return p^; }
// NRVO is the ELIDE of that pair: one object, built in the caller's slot, ctor NOT on top.
Ret retNrvo() { Ret a(0); a.v_ = 6; return a; }
// an ARRAY literal of class lvalues — the same per-slot order down the OTHER bridge
// (emitArrayFromTuple, not emitTupleFromTuple).
Ret[2] retArr() {
    Ret a(0); a.v_ = 7;
    Ret b(0); b.v_ = 8;
    return (a, b);
}
// NRVO DECLINES on the `_$ret` local: `t` is a returned-local candidate and is still LIVE
// where the rewritten return sits, so both are ineligible and both take the codegen path
// (construct the slot, then transfer into it). Correct either way — one extra object.
(Ret, Ret) retDecline(bool b) {
    (Ret, Ret) t;
    t[0].v_ = 9;
    t[1].v_ = 10;
    Ret a(0); a.v_ = 11;
    Ret c(0); c.v_ = 12;
    if (b) { return t; }
    return (a, c);
}
// a FIELD and an INDEX source — lvalues that are not bare idents.
Bx(Ret r_) { _() { } ~() { } }
Ret retField(Bx^ h)      { return h^.r_; }
Ret retIndex(Ret[2]^ a)  { return a^[1]; }

// A class CHAIN in a returned SLOT. The slot is peeled (a chain cannot be handed a slot of a
// literal — its accumulator home is answered per STATEMENT), constructed, and the chain is
// then assigned INTO it. Chn's ctor writes 99, so a chain landing in a filled-then-
// constructed slot reads back 99 instead of the sum.
Chn(int v_) {
    _()                   { v_ = 99; }
    ~()                   { }
    op=(Chn^ r)           { v_ = r^.v_; }
    op+(Chn^ x, Chn^ y)   { v_ = x^.v_ + y^.v_; }
    int get()             { return v_; }
}
(Chn, Chn) retChain() {
    Chn a(0); a.v_ = 1;
    Chn b(0); b.v_ = 2;
    Chn r(0); r.v_ = 5;
    return (a + b, r);            // slot 0 = a CHAIN, slot 1 = an LVALUE transfer
}

// THE TRANSFER INVARIANT AT THE RETURN SLOT: the fallback must call the AUTHOR'S op<--, not
// blit past it. Op prints "move" and adds 200 — mkOp() above is NRVO'd, so nothing else in
// this file makes a return actually TRANSFER through a user operator.
(Op, Op) mkOpPair() { Op a(1); Op b(2); return (a, b); }
Op pickOp(bool b)   { Op a(1); Op c(2); if (b) { return a; } return c; }

int32 main() {

    /* POD aggregate — decl (build a new local), existing var, and inline use. */
    int a[3] = mkArr();
    __println("a= " + a[0] + " " + a[1] + " " + a[2]);          // 1 2 3
    (int, int) t = mkTup();
    __println("t= " + t[0] + " " + t[1]);                       // 4 5
    int a2[3];
    a2 = mkArr();
    __println("a2= " + a2[0] + " " + a2[2]);                    // 1 3

    /* hook class — decl (mkClass elides straight into x). ctor/dtor must balance. */
    __println("-- class decl --");
    {
        Class x = mkClass();
        __println("x= " + x.id_);                               // 7
    }
    __println("-- class pair --");
    {
        (Class, Class) p = mkPair();
        __println("p= " + p[0].id_ + " " + p[1].id_);           // 8 9
    }
    /* hook class — assign into an EXISTING var (temp + move-assign, no elision).
       y is constructed (1), then overwritten by the moved-in result (7); counts
       still balance (the default move-assign is a whole-value overwrite). */
    __println("-- class assign --");
    {
        Class y(1);
        y = mkClass();
        __println("y= " + y.id_);                               // 7
    }
    /* ELIDE-WHENEVER-POSSIBLE — a class defining op= / op<-- ELIDES a call rvalue into a
       FRESH decl (RVO — the operator does NOT run), and dispatches the OPERATOR only on
       an EXISTING variable. Across {copy, move} x {typed decl-init, inferred decl-init,
       existing var}: each decl-init elides (stays 7); each existing-var assign fires the
       operator (7 -> 107 copy / 207 move). ctor/dtor balances throughout. */
    __println("-- elide typed decl copy --");
    {
        Op cd = mkOp();                                         // typed decl -> elide
        __println("cd= " + cd.v_);                              // 7
    }
    __println("-- elide inferred decl copy --");
    {
        ce = mkOp();                                            // inferred decl -> elide
        __println("ce= " + ce.v_);                              // 7
    }
    __println("-- elide existing copy --");
    {
        Op ex(1);
        ex = mkOp();                                            // existing var -> op=
        __println("ex= " + ex.v_);                              // 107
    }
    __println("-- elide typed decl move --");
    {
        Op cm <-- mkOp();                                       // typed decl -> elide
        __println("cm= " + cm.v_);                              // 7
    }
    __println("-- elide inferred decl move --");
    {
        cn <-- mkOp();                                          // inferred decl -> elide
        __println("cn= " + cn.v_);                              // 7
    }
    __println("-- elide existing move --");
    {
        Op em(1);
        em <-- mkOp();                                          // existing var -> op<--
        __println("em= " + em.v_);                              // 207
    }
    /* INLINE use — a hook call in an expression position (an argument) is lifted to
       a temp decl by desugar, so it constructs + is destroyed cleanly. */
    __println("-- inline arg --");
    {
        __println("arg= " + idOf(mkClass()));                   // 7
    }
    /* RETURN-OF-CALL — wrapClass forwards its slot to mkClass (no extra object). */
    __println("-- return of call --");
    {
        Class w = wrapClass();
        __println("w= " + w.id_);                               // 7
    }

    /* ARRAY-of-class return (Class[3]) — NRVO builds the array in place. */
    __println("-- array of class --");
    {
        Class xa[3] = mkArrC();
        __println("xa= " + xa[0].id_ + " " + xa[2].id_);        // 10 30
    }
    /* MIXED (Class, int) and NESTED (Class[2], int) returns (rvalue tuples). */
    __println("-- mixed --");
    {
        (Class, int) m = mkMixed();
        __println("m= " + m[0].id_ + " " + m[1]);               // 5 9
    }
    __println("-- nested --");
    {
        (Class[2], int) n = mkNested();
        __println("n= " + n[0][0].id_ + " " + n[0][1].id_ + " " + n[1]);  // 6 7 8
    }

    /* a class with a POINTER leaf — returned (NRVO) and assigned into an existing
       var (the fallback move, which nulls the source's pointer leaves). */
    __println("-- pointer leaf --");
    {
        int v0 = 5;
        Holder h = mkHolder(^v0);
        __println("h= " + h.p_^);                               // 5
        int v1 = 6;
        Holder h2(^v0);
        h2 = mkHolder(^v1);                                     // existing-var move-assign
        __println("h2= " + h2.p_^);                             // 6
    }

    /* POD aggregate shapes the spec lists, plus a non-exact leaf-widen return. */
    __println("-- pod shapes --");
    {
        (int[3], int[3]) pn = mkPodNest();
        __println("pn= " + pn[0][0] + " " + pn[1][2]);          // 1 6
        (int, int) pa[2] = mkPodAoT();
        __println("pa= " + pa[0][0] + " " + pa[1][1]);          // 1 4
        (int, int) w2 = mkWiden();
        __println("w2= " + w2[0] + " " + w2[1]);                // 1 2
    }

    /* NRVO with two returns of the SAME local (cs); a disjoint multi-local return
       (g — NRVO'd, one object); and an overlapping multi-local return (bd — falls
       back to a move, two objects). */
    __println("-- nrvo variants --");
    {
        Class cs = condSame(true);
        __println("cs= " + cs.id_);                             // 3
        Class g = pickGood(true);
        __println("g= " + g.id_);                               // 1
        Class bd = pickBad(false);
        __println("bd= " + bd.id_);                             // 2
        Class g2 = pickGood2(true);                            // if/else disjoint -> NRVO
        __println("g2= " + g2.id_);                             // 1
        Class bd2 = pickBad2(false);                           // outer-alive -> fallback
        __println("bd2= " + bd2.id_);                           // 2
    }

    /* CALL-INIT local NRVO-returned (mkFromCall): mkClass builds through mkFromCall's
       aliased slot straight into nc — exactly ONE ctor 7, and nc owns the ONE dtor 7 at
       block exit. (A frame that double-registered the nrvo slot would print dtor 7 twice.) */
    __println("-- nrvo call-init --");
    {
        Class nc = mkFromCall();
        __println("nc= " + nc.id_);                            // 7
    }

    /* NESTED inline calls — idOf(bump(^a)): the inner call is lifted, the result
       fed to idOf by reference. */
    __println("-- nested inline --");
    {
        Class a3(1);
        __println("ni= " + idOf(bump(^a3)));                    // 2
    }

    /* DISCARDED hook call — constructed then destroyed at the call seam. */
    __println("-- discard --");
    {
        mkClass();
    }

    /* existing-var assign of a TUPLE-of-class (move-assign whole aggregate). */
    __println("-- exist pair --");
    {
        (Class, Class) p = (0, 0);
        p = mkPair();
        __println("ep= " + p[0].id_ + " " + p[1].id_);          // 8 9
    }

    /* an aggregate-returning call in a LOOP — the result temp/slot is reused. */
    __println("-- loop --");
    {
        for (i : 0..2) {
            Class li = mkClass();
            __println("li= " + li.id_);                         // 7, 7
        }
    }
    /* RETURN VALUE USED IN AN EXPRESSION — a call result as an operand of aggregate
       arithmetic (either side), as a call argument, and accumulated in a loop. The
       result is materialized in a temp (stacksave-bracketed) and fed to the
       surrounding expression. retLit() also covers returning a tuple LITERAL as an
       array (the rvalue-return path through emitExpr). */
    __println("-- return in expr --");
    {
        int e1[3] = retLit() + (10, 20, 30);
        __println("e1= " + e1[0] + " " + e1[2]);                // 11 33
        int e2[3];
        e2 = mkArr() + (1, 1, 1);                               // call result, assign rhs
        __println("e2= " + e2[0] + " " + e2[2]);                // 2 4
        (int, int) e3 = mkTup() + (100, 100);                  // call on the LEFT
        __println("e3= " + e3[0] + " " + e3[1]);                // 104 105
        (int, int) e4 = (100, 100) + mkTup();                  // call on the RIGHT
        __println("e4= " + e4[0] + " " + e4[1]);                // 104 105
        int sp = sumPair(mkTup());                             // call result as an arg
        __println("sp= " + sp);                                 // 9
        (int, int) acc = (0, 0);
        for (i : 0..3) { acc = acc + mkTup(); }                // accumulate in a loop
        __println("acc= " + acc[0] + " " + acc[1]);            // 12 15
        /* hook call result in an expression (an arg) — lifted to a temp. */
        __println("ho= " + idOf(mkClass()));                    // 7
    }

    /* A CLASS RVALUE TRANSFERRED INTO LIVE STORAGE. These were negatives: a live target has
       no fresh slot to BUILD into, so a class-returning CALL (and a construction) as the rhs
       of an assign / move / store was rejected. But a live target does not want to be built
       — it wants to be TRANSFERRED into, and the source only has to be materialized first.
       It becomes a temporary, the target's operator copies / moves from it, and the temp dies
       at the SEMICOLON. Op's operators PRINT and add 100 / 200, so it is visible that the
       author's operator really ran (rather than a field-init over the live object). */
    {
        __println("-- rvalue into live storage --");
        Op x(1);
        x = mkOp();                                            // call, into an existing var
        __println("x= " + x.v_);                                // 107 (op= added 100)
        Op y(2);
        y <-- mkOp();                                          // call, moved in
        __println("y= " + y.v_);                                // 207 (op<-- added 200)
        Op z(3);
        z = Op(33);                                            // construction, into a live var
        __println("z= " + z.v_);                                // 133
        Op w(4);
        w <-- Op(44);                                          // construction, moved in
        __println("w= " + w.v_);                                // 244
        Op arr[2] = (5, 6);
        arr[0] = mkOp();                                       // into a store target
        __println("arr0= " + arr[0].v_);                        // 107
    }
    /* THE RETURN SLOT IS CONSTRUCTED BEFORE IT IS TRANSFERRED INTO. Ret's ctor writes
       v_ = 99, so a slot that is FILLED and then CONSTRUCTED reads back 99 — the ctor
       landing on top of the moved-in value. Every case here must read back what the
       function put there. */
    {
        __println("-- return slot order --");
        (Ret, Ret) rp = retPair();
        __println("rp= " + rp[0].v_ + " " + rp[1].v_);          // 1 2
        (Ret, Ret) rm = retMixed();
        __println("rm= " + rm[0].v_ + " " + rm[1].v_);          // 3 99 (slot 1 is BUILT)
        Ret r1 = retPick(true);
        Ret r2 = retPick(false);
        __println("rk= " + r1.v_ + " " + r2.v_);                // 4 5
        Ret rn = retNrvo();
        __println("rn= " + rn.v_);                              // 6
        Ret rr = retParam(^rn);
        __println("rr= " + rr.v_);                              // 6

        Ret ra[2] = retArr();
        __println("ra= " + ra[0].v_ + " " + ra[1].v_);          // 7 8
        (Ret, Ret) dt = retDecline(true);                       // NRVO declines on both
        (Ret, Ret) df = retDecline(false);
        __println("dt= " + dt[0].v_ + " " + dt[1].v_
                + " df= " + df[0].v_ + " " + df[1].v_);         // 9 10 / 11 12
        Bx bx;
        bx.r_.v_ = 12;
        Ret rf = retField(^bx);
        Ret rarr[2];
        rarr[1].v_ = 13;
        Ret rx = retIndex(^rarr);
        __println("rf= " + rf.v_ + " rx= " + rx.v_);            // 12 13
        (Chn, Chn) ch = retChain();
        __println("ch= " + ch[0].get() + " " + ch[1].get());    // 3 5
    }

    /* THE TRANSFER INVARIANT AT THE RETURN SLOT — the fallback dispatches the AUTHOR'S
       op<--, which prints "move" and adds 200. The slot is CONSTRUCTED first (ctor 0), the
       source is left a husk (v_ = 0, so it dtors as 0), and the caller owns the result. */
    {
        __println("-- return slot operator --");
        (Op, Op) op2 = mkOpPair();
        __println("op2= " + op2[0].v_ + " " + op2[1].v_);        // 201 202
        Op op1 = pickOp(false);
        __println("op1= " + op1.v_);                             // 202
    }
    __println("-- done --");
    return 0;
}

/* compile errors — each uncommented in isolation by the negative runner. */

/* A CLASS RVALUE INTO LIVE STORAGE IS SUPPORTED (the positives above): a store target
   (`arr[0] = mkClass()`), a move operand (`a <-- mkClass()`), and a construction into an
   existing variable (`x = Op(11)` / `x <-- Op(11)`) all materialize the source as a
   temporary and TRANSFER it in through the target's operator. They used to be rejected on
   the grounds that a live target has no fresh slot to build into — which is the reason for
   the ELIDE, not a reason to refuse the transfer. A hook-returning call in a CONDITION is
   supported too (lifted into the condition's seq, rebuilt per evaluation). */
