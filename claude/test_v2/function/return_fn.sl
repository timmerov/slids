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

the call site has two (or three) options.

if the return type of the function and the type of the lhs match exactly,
and either there is no ctor at any leaf, or the lhs is being created,
then the call can be completed in place.

case 1: types match exactly, the lhs is being created here:

    Type obj = fn();

desugars to:

    allocate space for obj.
    fn_$desugar(^obj);

case 2: types match exactly, the lhs has no ctor in any leaf (plain old data):

    Type obj;
    obj = fn();

desugars to:

    Type obj;
    fn_$desugar(^obj);

case 3 (the fallback): types do not match or the lhs has already been ctor constructed.
move semantics must be used.

    OtherType obj;
    obj = fn();

desugars to:

    OtherType obj;
    {
        alloca $temp;
        fn_$desugar(^$temp);
        obj <-- $temp;
        $temp.dtor();
    }

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

PHASE A — always-sret + both-side FALLBACKS (no optimization yet).

A function returning a non-primitive type (array / tuple / class, or an aggregate
with a class leaf) is ALWAYS desugared to `void fn_$desugar(Type^ ret)`. The body
writes the result into `ret^` and never destructs it (the caller owns the dtor).
Phase A uses the FALLBACK on both sides:
  - FUNCTION: each `return E` default-move-inits `ret^` from E (field-move then
    ctor), so `ret^` is constructed exactly once and the ctor sees the returned
    values. No in-place RVO/NRVO yet.
  - CALLER: every call materializes a `_$ret` temp, calls `fn(^_$ret, args)`, then
    default-moves the temp into the lhs and destroys the temp. An inline use is the
    same temp, consumed in place (caller case 1 into a temporary).

Because nothing is elided yet, a class return materializes the MOST objects it ever
will (each constructed once / destroyed once) — Phase B drops the call-site temp,
Phase C (NRVO) drops the function-local. The invariant verified at every phase:
ctor count == dtor count. The classes below print id on ctor/dtor so the balance
is visible in the golden.
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

// NRVO variants: the SAME local returned from two paths (NRVO applies); and
// multi-local returns — disjoint scopes (good) and overlapping (bad) — which both
// fall back to a move (correct; the disjoint case is a deferred NRVO refinement).
Class condSame(bool b) { Class c(3); if (b) { return c; } return c; }
Class pickGood(bool b) { if (b) { Class p(1); return p; } Class q(2); return q; }
Class pickBad(bool b) { Class a(1); Class b2(2); if (b) { return a; } return b2; }

int32 main() {

    /* POD aggregate — decl (build a new local), existing var, and inline use. */
    int a[3] = mkArr();
    __println("a= " + a[0] + " " + a[1] + " " + a[2]);          // 1 2 3
    (int, int) t = mkTup();
    __println("t= " + t[0] + " " + t[1]);                       // 4 5
    int a2[3];
    a2 = mkArr();
    __println("a2= " + a2[0] + " " + a2[2]);                    // 1 3

    /* hook class — decl (Phase A: temp + move into x). ctor/dtor must balance. */
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
    /* hook class — assign into an EXISTING var (Phase A: temp + move-assign).
       y is constructed (1), then overwritten by the moved-in result (7); counts
       still balance (the default move-assign is a whole-value overwrite). */
    __println("-- class assign --");
    {
        Class y(1);
        y = mkClass();
        __println("y= " + y.id_);                               // 7
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

    /* NRVO with two returns of the SAME local; multi-local disjoint (good) and
       overlapping (bad) — both correct via the fallback. */
    __println("-- nrvo variants --");
    {
        Class cs = condSame(true);
        __println("cs= " + cs.id_);                             // 3
        Class g = pickGood(true);
        __println("g= " + g.id_);                               // 1
        Class bd = pickBad(false);
        __println("bd= " + bd.id_);                             // 2
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
    __println("-- done --");
    return 0;
}

/* compile errors — each uncommented in isolation by the negative runner. A hook-
   returning call in an expression position the lift does NOT cover yet (a store
   target, a loop / if condition, a move/swap operand) is rejected cleanly rather
   than miscompiled. These flip to positives when those positions are lowered. */

/* store target: `arr[i] = mkClass()`. */
//-EXPECT-ERROR: Returning a class by value in an expression position is not yet supported
//int neg_store_rhs() {
//    Class arr[2] = (1, 2);
//    arr[0] = mkClass();
//    return arr[0].id_;
//}

/* an if-condition (the lift does not descend conditions). */
//-EXPECT-ERROR: Returning a class by value in an expression position is not yet supported
//int neg_cond() {
//    if (idOf(mkClass()) > 0) { return 1; }
//    return 0;
//}

/* a move operand. */
//-EXPECT-ERROR: Returning a class by value in an expression position is not yet supported
//int neg_move_rhs() {
//    Class a(1);
//    a <-- mkClass();
//    return a.id_;
//}
