/*
test operator overload templates.

excludes: op= op<-- op<-->.

definition:

    return-type op operator < template-list > ( parameter-list ) { body }

template-list is a comma separate list of identifiers used as types.
the optional return type maybe a type from the template-list.
parameter-list is a normal comma separated list of parameters.
the parameters may use types from the template-list.
operator overload templates are subject to the same restrictions as plain
operator overloads - including return-type, arity, parameter types, etc.

usage:

    Vec(float x, float y) {
        op*<T>(Vec^ a, T b) {
            x = a^.x * b;
            y = a^.y * b;
        }
    }

    Vec v1(1.0, 2.0);
    Vec v2 = v1 * 3;

operator overloads are special case of methods.
they are subject to same rules as plain and template methods.
including the conventions of convenience for passing objects as references,
and rewriting type arguments are reference to cons.
see tmpl_method.sl.
*/

/*
claude says:

a template operator is a template method named `op<sym>` — the whole method-
template machinery (pattern + snapshot + classify-time instantiation +
memoization + the conventions of convenience) rides unchanged. dispatch binds
by INFERENCE ONLY: an operator use has no type-list position. the grammar's
one change: the template-list parses after the max-munched symbol, so the
`<`-family needs whitespace (`op< <T>`; `op<<T>` is the SHIFT symbol).

selection is uniform with methods: PLAIN BEATS TEMPLATE (the plain overload
set is probed first; the template binds what no plain takes; direct plain >
direct template > coerced plain), and same-name template operators overload
by ARITY only. the comparison / index / unary families dispatch through the
method funnel and inherit all of it; the binary CHAIN and the compound-assign
funnel carry their own lazy template fallbacks. type-dependent operator
restrictions run per INSTANCE (a pattern is unchecked until bound).

excluded: template op= / op<-- / op<--> (rejected at registration — the
value-init probe and the transfer invariant's canonical operators take
concrete types). a template operator with T in NO parameter is unusable
(nothing infers it; operators take no explicit list).
*/

/* the canon workhorse: scalar scaling over any numeric T. */
Vec(float64 x_ = 0.0, float64 y_ = 0.0) {
    op*<T>(Vec^ a, T b) {
        x_ = a^.x_ * b;
        y_ = a^.y_ * b;
    }
}

/* PLAIN BEATS TEMPLATE on the compound family — the canon pair. */
Acc(int t_ = 0) {
    op+=(int b) { t_ = t_ + b + 500; }
    op+=<T>(T b) { t_ = t_ + 1; }
}

/* a template COMPARISON (dispatches through the method funnel), and a
   template op[] (the index sugar; the reference-return rule checks per
   instance). */
Cell(int c_ = 0, int alt_ = 0) {
    bool op==<T>(T o) { return c_ == o; }
    int^ op[]<T>(T i) {
        if (i == 0) { return ^c_; }
        return ^alt_;
    }
}

/* a template operator whose operand binds a CLASS — the convention of
   convenience: the instance takes `(const Pt)^`, uses auto-deref. */
Pt(int p_ = 0) { }
Sum(int s_ = 0) {
    op+=<T>(T b) { s_ = s_ + b.p_; }
}

/* the `<`-family spelling: the symbol is max-munched, so the templated
   comparison needs the space (`op< <T>`). */
Ord(int o_ = 0) {
    bool op< <T>(T x) { return o_ < x; }
}

/* the OUT-OF-LINE template operator form. */
Ool(int v_ = 0) { }
Ool:op*<T>(Ool^ a, T b) { v_ = a^.v_ * b; }

/* a template operator in a CLASS-TEMPLATE flavor (uniform, per flavor). */
Box<T>(T b_ = 0) {
    op+=<S>(S s) { b_ = b_ + 1; }
    T get() { return b_; }
}

/* same-symbol template operators overload by ARITY (binary 2 vs aug 1
   spelled as distinct symbols is the normal case; two 2-arg op* templates
   would collide — negative below). */
//-EXPECT-ERROR: may not share its name
//BadA(int a_ = 0) {
//    op*<T>(BadA^ p, T q) { a_ = p^.a_; }
//    op*<U>(BadA^ p, U q) { a_ = p^.a_; }
//}

/* the transfer family takes no template form... */
//-EXPECT-ERROR: is not supported
//BadEq(int a_ = 0) {
//    op=<T>(T r) { a_ = 1; }
//}

/* ...none of it. */
//-EXPECT-ERROR: is not supported
//BadMv(int a_ = 0) {
//    op<--<T>(mutable T^ r) { a_ = 1; }
//}

int32 main() {

    /* the canon example: T infers from the scalar operand. */
    Vec v1(1.0, 2.0);
    Vec v2 = v1 * 3;
    __println("a1 = " + v2.x_ + "," + v2.y_);
    Vec v3 = v1 * 2.5;
    __println("a2 = " + v3.x_ + "," + v3.y_);

    /* plain beats template on op+=: int takes the plain, int64 the template. */
    Acc acc;
    acc += 2; __println("b1 = " + acc.t_);
    int64 big = 7;
    acc += big; __println("b2 = " + acc.t_);

    /* a template comparison and a template op[]. */
    Cell cell(4, 9);
    if (cell == 4) { __println("c1 = eq"); }
    int8 idx = 1;
    int c2 = cell[idx]; __println("c2 = " + c2);
    cell[0] = 40;
    __println("c3 = " + cell.c_);

    /* the class operand rides the convention. */
    Sum sum;
    Pt pt(21);
    sum += pt; __println("d1 = " + sum.s_);

    /* the spaced `<`-family spelling. */
    Ord ord(5);
    if (ord < 9) { __println("e1 = lt"); }

    /* the out-of-line template operator. */
    Ool oo(6);
    Ool o2 = oo * 4;
    __println("f1 = " + o2.v_);

    /* per class-template flavor. */
    Box<int> bx(10);
    bx += 3; __println("g1 = " + bx.get());

    return 0;
}
