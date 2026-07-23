/*
test template class completion.

template classes may be incomplete, re-opened, and completed.
just like normal classes.
*/

/*
claude says:

a template's openings stay PATTERNS: the primary plus each re-open (which
must repeat the primary's template list, exactly) are recorded pristine, and
instantiation clones EVERY opening in source order — the clones then re-run
the plain-class merge machinery unchanged, so appended fields, contributed
hooks (a ctor in one opening, the dtor in another), member sets, and the
virtual re-open rules land per instance exactly as a plain class's openings
do. the open/close (`...`) state machine runs at the PATTERN level, so
"cannot add fields" / "already complete" fire at the declaration whether or
not anyone instantiates. external members (`T Ext:gete()`, `const int
Ext:kk`) relocate into the pattern before registration and ride every clone.

a never-completed incomplete class template is a compile error: a plain
class left open completes in another translation unit via a header, but
cross-TU templates have not landed, so the dangling `...` gets a focused
diagnostic instead of a silent half-class. a listless `Grow() { }` is still
a collision — every opening spells the template list.

hard-won: the instance entry's owner_ns_frame (the symbol's scope path) must
stamp AFTER every opening clone registers — findInFrame skips owner-bearing
entries, so stamping early hid the primary clone from a NAMESPACE-member
re-open's merge lookup, which then re-registered a second class and emitted
its synthesized transfer ops twice (invalid IR at llc).
*/

/* incomplete primary; an appending re-open; a closing re-open that adds a
   method reading fields from BOTH earlier openings. */
Grow<T>(T a_ = 1, ...) {
    T ga() { return a_; }
}
Grow<T>(T b_ = 2, ...) {
    T gb() { return b_ + a_; }
}
Grow<T>() {
    T gsum() { return a_ + b_; }
}

/* a COMPLETE template re-opened to add members: a method calling a primary
   method, and an operator. */
Vec2<T>(T x_ = 5) {
    T get() { return x_; }
}
Vec2<T>() {
    T dbl() { return get() * 2; }
    op+(Vec2^ p, Vec2^ q) { x_ = p^.x_ + q^.x_; }
}

/* the lifecycle spans openings: the ctor in the primary, the dtor in the
   closing re-open (which also appends a field). */
Tr<T>(T t_ = 0, ...) {
    _() { __println("tc " + t_); }
}
Tr<T>(T u_ = 1) {
    ~() { __println("td " + t_ + " " + u_); }
}

/* external members of a template: a method (T in its signature), a const,
   an alias, and an enum — each used by an in-body method. */
Ext<T>(T e_ = 3) {
    int viak() { return kk + gete(); }
    int viae() { EA q = EE:ee1; return q + 1; }
}
T Ext:gete() { return e_; }
const int Ext:kk = 40;
alias Ext:EA = int;
enum int Ext:EE ( ee1 = 7, ee2 );

/* a user transfer operator contributed by a RE-OPEN — must be called, never
   blitted (the +1 skew shows). */
Us2<T>(T u_ = 0) {
    T get() { return u_; }
}
Us2<T>() {
    op=(Us2^ s) { u_ = s^.u_ + 1; }
}

/* a hooked plain class, appended as a FIELD by a closing re-open: needs
   propagate through pending_fields; hooks balance. */
Datb(int d_ = 3) {
    _() { __println("db ctor " + d_); }
    ~() { __println("db dtor " + d_); }
    int dv() { return d_; }
}
Tr2<T>(T a_ = 1, ...) {
}
Tr2<T>(Datb d_) {
    int both() { return a_ + d_.dv(); }
}

/* an overload set spanning openings (same name, different params). */
Ov<T>(T o_ = 1) {
    T f(T v) { return v + o_; }
}
Ov<T>() {
    T f(T v, T w) { return v + w + o_; }
}

/* a virtual slot DECLARED in the primary, implemented by the re-open; a
   plain class derives from the re-opened virtual instance. */
Vb2<T>(T z_ = 1) {
    virtual T vg();
}
Vb2<T>() {
    virtual T vg() { return z_ + 6; }
}
Vb2<int> : Vk2(int y_ = 0) {
    virtual int vg() { return 100 + y_; }
}

/* a namespace-member template with an appending re-open, used qualified. */
Spc {
    NGrow<T>(T n_ = 4, ...) {
    }
    NGrow<T>(T m_ = 5) {
        T nsum() { return n_ + m_; }
    }
}

/* the ctor DECLARED in the primary, DEFINED by the re-open. */
Hk<T>(T h_ = 2, ...) {
    _();
    ~() { __println("hk dtor " + h_); }
}
Hk<T>() {
    _() { __println("hk ctor " + h_); }
}

/* an external member relocated while the template was still OPEN. */
Og<T>(T g1_ = 1, ...) {
}
int Og:extm() { return 21; }
Og<T>(T g2_ = 2) {
}

/* a re-opened template demanded from a function template's body. */
T mkg<T>(T v) {
    Grow<T> w(v, v);
    return w.gsum();
}

alias Integer = int;

/* a closed re-open cannot add fields... */
//-EXPECT-ERROR: cannot add fields
//Vec2<T>(T zz_ = 1) { }

/* ...nor re-open as incomplete. */
//-EXPECT-ERROR: already complete
//Vec2<T>(...) { }

/* a re-open's template list must match the primary's. */
//-EXPECT-ERROR: template list must match
//Grow<U>() {
//    U bad() { return 1; }
//}

/* a listless opening is no re-open — the template owns its name. */
//-EXPECT-ERROR: owns its name
//Grow(int zz_ = 0) { }

/* a never-completed incomplete template has no header to complete it. */
//-EXPECT-ERROR: never completed
//Openx<T>(T o_ = 1, ...) { }

/* a re-open may not re-declare a member with the same signature (fires at
   the instantiation main performs). */
//-EXPECT-ERROR: Duplicate definition
//Ov<T>() {
//    T f(T v) { return v; }
//}

/* `Ext:Zub() { }` with no member Zub reinterprets as inheritance from the
   bare template name — which requires a type-argument list. */
//-EXPECT-ERROR: requires a type-argument list
//Ext:Zub() {
//    int zz() { return 1; }
//}

int32 main() {

    /* defaults across all three openings, then explicit args (layout order:
       primary fields, then appended). */
    Grow<int> g;
    int ga = g.ga(); __println("ga = " + ga);
    int gb = g.gb(); __println("gb = " + gb);
    int gs = g.gsum(); __println("gs = " + gs);
    Grow<int> g2(10, 20);
    int ga2 = g2.ga(); __println("ga2 = " + ga2);
    int gb2 = g2.gb(); __println("gb2 = " + gb2);
    int gs2 = g2.gsum(); __println("gs2 = " + gs2);

    /* a second binding gets the same merged openings. */
    Grow<int8> g8(4, 5);
    int8 gs8 = g8.gsum(); __println("g8 = " + gs8);

    /* members added by a re-open: the method and the operator. */
    Vec2<int> v(6);
    int d1 = v.dbl(); __println("d1 = " + d1);
    Vec2<int> w = v + v;
    int d2 = w.get(); __println("d2 = " + d2);
    int d3 = w.dbl(); __println("d3 = " + d3);

    /* hooks contributed by different openings pair per class. */
    {
        Tr<int> tr(5, 6);
        __println("mid");
    }

    /* external members ride every instance. */
    Ext<int> ex;
    int x1 = ex.gete(); __println("x1 = " + x1);
    int x2 = ex.viak(); __println("x2 = " + x2);
    Ext<int> ex2(9);
    int x3 = ex2.viak(); __println("x3 = " + x3);

    /* block-scope: incomplete + completing re-open in a function body. */
    Lg<T>(T p_ = 2, ...) {
    }
    Lg<T>(T q_ = 3) {
        T lsum() { return p_ + q_; }
    }
    Lg<int> lg;
    int b1 = lg.lsum(); __println("b1 = " + b1);
    Lg<int> lg2(7, 8);
    int b2 = lg2.lsum(); __println("b2 = " + b2);

    /* a user op= from a re-open is called — the skew shows, no blit. */
    Us2<int> pa(5);
    Us2<int> pb = pa;
    __println("r1 = " + pb.get());

    /* a hooked class field appended by a re-open: constructed in place,
       destroyed exactly once. */
    {
        Tr2<int> t2(9, Datb(4));
        __println("r2 = " + t2.both());
    }

    /* the overload set spans openings. */
    Ov<int> ov(10);
    int r3 = ov.f(1); __println("r3 = " + r3);
    int r4 = ov.f(1, 2); __println("r4 = " + r4);

    /* the virtual slot declared in the primary dispatches to the re-open's
       implementation — directly and through a derived receiver. */
    Vb2<int> vb(2);
    int r5 = vb.vg(); __println("r5 = " + r5);
    Vk2 vk(3, 4);
    Vb2<int>^ vp = ^vk;
    int r6 = vp^.vg(); __println("r6 = " + r6);

    /* the namespace-member re-opened template, defaults and args. */
    Spc:NGrow<int> ng;
    int r8 = ng.nsum(); __println("r8 = " + r8);
    Spc:NGrow<int> ng2(20, 30);
    int r8b = ng2.nsum(); __println("r8b = " + r8b);

    /* the declared ctor's definition came from the re-open. */
    {
        Hk<int> hk(7);
        __println("hkmid");
    }

    /* an empty leading slot over the merged layout. */
    Grow<int> g3(,20);
    int r9 = g3.gsum(); __println("r9 = " + r9);

    /* a re-opened template instantiated from a function template's body — a
       fresh binding, all openings cloned at classify time. */
    int16 mg = 3;
    int16 r10 = mkg(mg); __println("r10 = " + r10);

    /* the external alias + enum members. */
    int r11 = ex.viae(); __println("r11 = " + r11);

    /* the external method relocated while the template was open. */
    Og<int> og;
    int r12 = og.extm(); __println("r12 = " + r12);
    int r13 = og.g1_ + og.g2_; __println("r13 = " + r13);

    /* the sweep: new, an array, a tuple slot, alias-argument dedup. */
    Grow<int>^ gp = new Grow<int>;
    int r14 = gp^.gsum(); __println("r14 = " + r14);
    Grow<int> garr[2];
    int r15 = garr[0].gsum() + garr[1].gsum(); __println("r15 = " + r15);
    Grow<int> g0;
    (Grow<int>, int) gt = (g0, 5);
    int r16 = gt[0].gsum() + gt[1]; __println("r16 = " + r16);
    Vec2<Integer> valias(6);
    int r17 = valias.dbl(); __println("r17 = " + r17);

    /* an appending re-open may not duplicate a field. */
    //-EXPECT-ERROR: Duplicate field
    //Df<T>(T da_ = 1, ...) {
    //}
    //Df<T>(T da_ = 2) {
    //}
    //Df<int> df;
    //int qd = df.da_; __println("qd = " + qd);

    /* never-completed at block scope, with a use — the diagnostic fires at
       the use site. */
    //-EXPECT-ERROR: never completed
    //Ob<T>(T o_ = 1, ...) {
    //}
    //Ob<int> ob;
    //int qb = ob.o_; __println("qb = " + qb);

    /* a re-open may not add a NEW virtual method. */
    //-EXPECT-ERROR: may not add the new virtual method
    //V3<T>(T v_ = 0) {
    //    virtual T vm() { return v_; }
    //}
    //V3<T>() {
    //    virtual T vnew() { return 1; }
    //}
    //V3<int> v3x;
    //int q9 = v3x.vm(); __println("q9 = " + q9);

    return 0;
}
