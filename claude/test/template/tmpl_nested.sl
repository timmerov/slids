/*
test templates declared within templates.

not all template nesting is supported for practical reasons.

examples (includes reach and aspirational):
    template alias inside template class, function, method.
    template method inside a template class.
    template type inside a template type list.
    template class inside a template class, function, method
    local template function inside template function, method.

template inside template list.
the lex/parse challenge here is >> is right shift.

    Vector<T>(T t_) { }
    Vector< Vector<int> > vvi;
    Vector< Vector<int >> not_right_shift;

template method inside template class.

    TClass<T>(T t_) {
        S smethod<S>(S s) {
            __println("t=" + t_ + " s=" + s)
            return s;
        }
    }

generally, you must use fully qualified type list to access members
of a template class.
however, some members of a template class have no dependency on any
type in the list.
they may use empty list qualifier syntax.

    TClass<T>(T t_) {
        alias Integer = int;
        alias RefT = T^;
        alias Type<S> = S;
        HoistT(T t_) { }
        HoistS<S>(S s_) { }
        HoistI(int i_) { }
    }

    TClass<>:Integer i;
    TClass<RandomType>:Integer i;
    TClass<int>:RefT ref;
    TClass<>:Type<float> x;
    TClass<RandomType>:Type<float> x;

    TClass<int>:HoistT a;
    TClass<>:HoistS<float> b;
    TClass<RandomType>:HoistS<float> c;
    TClass<>:HoistI d;
    TClass<RandomType>:HoistI e;

note: these are different types:

    TClass<>:HoistI a;
    TClass<int>:HoistI a;
    TClass<float>:HoistI a;
    TClass<RandomType>:HoistI a;

note: this applies to everything that can be declared inside a template
class - including: enums and globals.

reach goal not specific to nesting: these are different types.

    alias Float = float;
    TClass<float> a;
    TClass<Float> a;

previous canon being repealed:

hoisted template class inside template class.
the hoisted class template list is independent of the host
class template list.
in the example, SClass cannot use type T.
UClass cannot use the type T from TClass.
it uses the type T from its own template list.
the qualifier is the bare host class name without the host
class template list.
it's neither needed nor available to the hoisted class.
therefore, it should not be in the syntax.

    TClass<T>(T t_) {
        SClass<S>(S s_) { }
        UClass<U, T>(U u_, T t_) { }
    }
    TClass:SClass<int> sobj;
    TClass:UClass<int, float> uobj;
*/

/*
claude says:

a template method inside a class template rides the machinery that already
exists on both sides: the flavor clone re-runs plain-class registration, whose
member divert registers the method template PER FLAVOR (its own pattern entry,
owned by the instance's frame); the pattern's snapshot is taken while the
flavor's T-alias frame is installed, so the outer T is bound inside the
snapshot by construction — the inner list binds only the method's own params
at the call, exactly like a plain class's template method. each flavor's
patterns are independent; instances memoize per flavor per binding. the plain
rules re-fire per flavor: a template method owns its name, virtual is
rejected, a re-open opening's template method lands with the merge.

EVERY nested member is PER FLAVOR (the 2026-07-26 repeal): a nested class
or alias template registers NO file-scope sub-pattern — the flavor clone
re-registers it per instance through the ordinary member diverts, where the
flavor's own list is bound. so the QUALIFIER is the outer parameters' whole
binding surface: the inner list binds only the member's own params (re-using
an outer's name there SHADOWS it — innermost wins), and an unlisted outer
param simply arrives from the qualifier (`Outc<int>:UsesT<float>` binds T=int
with no re-listing). the sub-pattern's snapshot is taken EAGERLY at flavor
minting — the T-alias frame and self-redirect are live there — because a use
may instantiate it before the drain's body phase. distinct qualifiers mint
DISTINCT TYPES, even for members with no list dependency, and a flavor's
enums, consts, and GLOBALS are per-flavor too — a global is distinct STORAGE
per qualifier. inside its own bodies the bare inner name still means the
instance (the tmpl_self spelled-name redirect); it resolves nowhere outside.

the LISTLESS flavor `TClass<>` is the one spelling for members with no
dependency on the list. its clone strips fields, hooks, methods, and the
base UNCONDITIONALLY (no `TClass<>` object can exist, so nothing could ever
receive them), then strips the remaining members by NAME-BASED dependency,
iterated to a fixpoint (conservative: a coincidental re-use of a stripped
name reads as dependent). `TClass<>` never declares storage, never binds a
template argument, never constructs; a stripped member's use gets the
dependency rule and the real-list remedy; a bare pattern qualifier
(`TClass:member`) is a compile error naming both remedies. `<>` composes at
depth (`Host2<int>:H<>:kH`) — a nested pattern's listless flavor rides the
same machinery.

still rejected: `<>` on a header-declared template, a template method in a
HEADER-owned class template (the cross-TU bundle), and nested anything in a
header-owned template. (The out-of-line form targeting a template LANDED
2026-07-26 — the binder-list head, tmpl_class.sl.)
*/

/* the canon workhorse: a template method mixing the outer T and its own S;
   the two canon HOISTED class templates — SClass's list is its own alone,
   UClass uses the outer T UNLISTED (the qualifier binds it); and the canon
   <> member set (aliases, an alias template, plain nested classes, an enum,
   a const, a global). */
TClass<T>(T t_) {
    S smethod<S>(S s) {
        __println("t=" + t_ + " s=" + s);
        return s;
    }
    SClass<S>(S s_) { }
    UClass<U>(U u_, T t_) { }
    /* arity-only overloading inside a class-template flavor. */
    S choose<S>(S s) { return s; }
    S choose<S>(S a, S b) { return a + b; }
    /* hoisted ALIAS templates: own list binds its own params; an unlisted
       outer param in the target arrives from the qualifier's binding. */
    alias Ref<S> = S^;
    alias Wide<S> = T;
    /* the canon <> member set: list-independent members reachable through
       the LISTLESS flavor; the dependent ones (RefT, HoistT) are not. */
    alias Integer = int;
    alias RefT = T^;
    alias Type<S> = S;
    HoistT(T h_ = 0) { }
    HoistS<S>(S s_ = 0) { }
    HoistI(int i_ = 0) { }
    enum Color ( kRed, kGreen );
    const int kTc = 7;
    global int g_ = 0;
}

/* an alias template inside a template FUNCTION body, and inside a template
   METHOD body — block-scope registration when the instance's body resolves. */
X viaAlias<X>(X v) {
    alias Rp<S> = S^;
    Rp<X> r = ^v;
    return r^;
}
Meth<M>(M m_ = 0) {
    X pick<X>(X x) {
        alias Same<S> = S;
        Same<X> t = x;
        return t;
    }
}

/* a CLASS template and a LOCAL FUNCTION template inside a template
   FUNCTION's body — block-scope registration when the instance resolves,
   instantiated on the enclosing binding. */
X viaLocal<X>(X v) {
    Loc2<L>(L l_ = 0) { L get() { return l_; } }
    Loc2<X> lo(v);
    return lo.get();
}
Y viaFn<Y>(Y v) {
    L dub<L>(L l) { return l + l; }
    return dub(v);
}

/* ...and both inside a template METHOD's body, composed. */
Meth2<M>(M m_ = 0) {
    Y meth<Y>(Y y) {
        Lm<L>(L l_ = 0) { L get() { return l_; } }
        L tri<L>(L l) { return l + l + l; }
        Lm<Y> lo(y);
        return tri(lo.get());
    }
}

/* a hoisted template with METHODS: its own bare name is the receiver type;
   its own P binds from its list, the outer T from the qualifier's binding. */
Host<T>(T h_ = 0) {
    Pack<P>(P p_ = 0, T q_ = 0) {
        P total(T extra) { P r = p_ + q_ + extra; return r; }
        P dbl() { return p_ + p_; }
    }
}

/* the inner list RE-USES the outer's param name: innermost binding wins —
   the method's own T is the call's T, not the flavor's. */
Sh<T>(T t_ = 0) {
    T same<T>(T v) { return v; }
}

/* the CONVENTION OF CONVENIENCE reaches a member template's OUTER param: `T`
   spelled bare in the member's own signature converts by the FLAVOR's
   binding (the enclosing list arrives through the snapshot's self-redirect). */
Cp(int c_ = 0) { }
Depth<T>(T d_ = 0) {
    int both<S>(S s, T t) { return s.c_ + t.c_; }
}

/* lifecycle hooks in a hoisted template (the outer T unlisted); a hoisted
   instance as a plain class's FIELD, filled by the qualified construction
   EXPRESSION. */
Ho<T>(T unused_ = 0) {
    Sub<S>(S s_ = 0, T t_ = 0) {
        _() { __println("sub ctor " + s_); }
        ~() { __println("sub dtor " + s_); }
        S sum() { return s_ + t_; }
    }
}
Wrap(Ho<int8>:Sub<int> f_) { }

/* a template method self-recursing inside a flavor (memo seeded first). */
Rec<T>(T r_ = 0) {
    S fact<S>(S n) { if (n <= 1) { return 1; } return n * fact(n - 1); }
}

/* the canon tier-3 workhorse: templates inside template LISTS — the spaced
   forms and the max-munched `>>` closer, split at the parse. */
Vector<T>(T v_ = 0) {
    T get() { return v_; }
}
alias Rf<T> = T^;
int idOf<X>(X^ p, int v) { p; return v; }

/* the binding surface, each direction: outer-T-only, own-list-only,
   explicit-only, and a template method composing plain + template siblings. */
Box<T>(T v_ = 0) {
    T viaT<X>(X x) { T r = v_ + x; return r; }
    S echo<S>(S s) { return s; }
    S seed<S>() { S r = 7; return r; }
    int plain(int v) { return v + 1; }
    T mix<X>(X x) { T r = v_ + plain(x) + echo(x); return r; }
}

/* two class type-params under one method template. */
Pair<K, V>(K k_ = 0, V v_ = 0) {
    S pick<S>(S s) { S r = s + k_ + v_; return r; }
}

/* a re-open opening contributes the template method (every opening clones). */
Gauge<T>(T g_ = 0) {
    T raw() { return g_; }
}
Gauge<T>() {
    S twice<S>(S s) { return s + s; }
}

/* PLAIN BEATS TEMPLATE inside a class-template flavor too: the plain wins
   when it matches; the template takes the rest. */
CoexF<T>(T f_ = 0) {
    int m(int v) { return v + 100; }
    U m<U>(U v) { return v; }
}

/* HOISTED templates with BASES — a file-scope base and an instance base
   bound from the hoisted class's OWN list (`VW<S>` binds S from HV's list)
   — and DEPTH-2 nesting: a template inside a hoisted template, and a
   template inside a PLAIN nested class (P depends on the outer T; H does
   not, so H is <>-reachable and P is not). */
HBase(int hb_ = 1) {
    int bump() { return hb_ + 10; }
}
VW<S>(S z_ = 3) {
    virtual S tagv() { return z_; }
}
Host2<T>(T t_ = 2) {
    HBase : HD<S>(S g_ = 9) { S gv() { return bump() + g_; } }
    VW<S> : HV<S>(S k_ = 2) { virtual S tagv() { return 200 + k_; } }
    H<S>(S h_ = 8) {
        S hv() { return h_; }
        D<U>(U d_ = 4) { U dv() { return d_; } }
        /* depth-2 DERIVED, a depth-2 ALIAS template, a depth-2 const. */
        HBase : G<U>(U g2_ = 1) { U g2v() { return bump() + g2_; } }
        alias R2<U> = U^;
        const int kH = 40;
    }
    P(T p_ = 3) {
        E<U>(U e_ = 6) { U ev() { return e_; } }
    }
}

/* a deep flavor minted in ONE function answers qualified from another. */
void warmH() {
    Host2<int>:H<int> wh(9);
    __println("wh = " + wh.hv());
}

/* a NAMESPACE-hosted template with a nested template: the chain crosses
   namespace -> pattern -> flavor -> sub-flavor. */
Spc3 {
    B<T>(T b_ = 3) {
        C<S>(S c_ = 6) { S cv() { return c_; } }
    }
}

/* a hoisted template's bare name resolves nowhere outside its host. */
//-EXPECT-ERROR: Unknown type
//int bads() { SClass<int> s; s; return 0; }

/* a LISTLESS pattern qualifier is out of the syntax (the repeal): only a
   flavor has members, so the message names both remedies — a real list,
   or `<>` for the list-independent members. At any depth. */
//-EXPECT-ERROR: requires a type-argument list
//int badh() { Host2:H:D<int> x(1); return x.dv(); }

/* ...the same rejection where a PLAIN nested class rides the chain
   (`Host2<int>:P:E<int>` is the spelling — P needs the outer flavor). */
//-EXPECT-ERROR: requires a type-argument list
//int badp() { Host2:P:E<int> x(1); return x.ev(); }

/* ...and on a direct member access. */
//-EXPECT-ERROR: requires a type-argument list
//int badq() { TClass:Integer i = 0; i; return 0; }

/* an unknown member at depth carets the deep prefix. */
//-EXPECT-ERROR: is not a type in
//int badn() { Host2<int>:H<int>:NoSuch x; x; return 0; }

/* a DEPENDENT member is outside the <> flavor — the use gets the rule and
   the real-list remedy. The nested-class form... */
//-EXPECT-ERROR: does not bind
//int badu() { TClass<>:HoistT h(1); h; return 0; }

/* ...the alias form... */
//-EXPECT-ERROR: does not bind
//int badal() { TClass<>:RefT p = nullptr; p; return 0; }

/* ...and the plain-nested-through-a-chain form. */
//-EXPECT-ERROR: does not bind
//int badpe() { Host2<>:P:E<int> x(1); return x.ev(); }

/* `TClass<>` is a qualifier, not a type: it declares no storage... */
//-EXPECT-ERROR: does not name a usable type
//int badv() { TClass<> v; v; return 0; }

/* ...and never binds a template-argument slot. */
//-EXPECT-ERROR: cannot be a template argument
//int badg() { Vector<TClass<>> vv; vv; return 0; }

/* DISTINCT TYPES per qualifier: byte-identical members of two flavors
   still never transfer into each other. */
//-EXPECT-ERROR: Cannot implicitly convert
//int badx() { TClass<>:HoistI a; TClass<int>:HoistI b(2); a = b; a; return 0; }

/* a nested template method checks its explicit arity like any template. */
//-EXPECT-ERROR: Wrong number of template arguments
//int bada() { TClass<int> t(1); return t.smethod<int, int>(5); }

/* the arithmetic convenience reaches template bindings: T=float + X=int
   inside viaT's body now converts (was the 'T=float'/'X=int' negative; the
   alias label=target message is pinned by expression/mixed.sl's comparison). */

int32 main() {

    /* the canon shape: two outer flavors, explicit and inferred inner
       bindings; the flavors' patterns bind independently. */
    TClass<int> ti(3);
    int e1 = ti.smethod<int>(5); __println("e1 = " + e1);
    int e2 = ti.smethod(6); __println("e2 = " + e2);
    float e3 = ti.smethod(2.5); __println("e3 = " + e3);

    TClass<float> tf(1.5);
    int e4 = tf.smethod(7); __println("e4 = " + e4);
    float e5 = tf.smethod<float>(3.5); __println("e5 = " + e5);

    /* one method instance serves every object of a flavor. */
    TClass<int> tj(9);
    int e6 = tj.smethod(5); __println("e6 = " + e6);

    /* binding surfaces. */
    Box<int> bx(10);
    int f1 = bx.viaT(2); __println("f1 = " + f1);
    int f2 = bx.echo(41); __println("f2 = " + f2);
    int f3 = bx.seed<int>(); __println("f3 = " + f3);
    int f4 = bx.mix(4); __println("f4 = " + f4);

    /* a second flavor mints an independent inner pattern. */
    Box<float> bf(0.5);
    float f5 = bf.viaT(1.5); __println("f5 = " + f5);
    float f6 = bf.echo(0.25); __println("f6 = " + f6);
    /* the arithmetic convenience through template bindings: T=float, X=int —
       `v_ + x` converts the int operand. */
    float f7 = bf.viaT(2); __println("f7 = " + f7);

    /* two class params under one method template. */
    Pair<int, int8> p(100, 27);
    int g1 = p.pick(1); __println("g1 = " + g1);

    /* the re-open's contributed template method beside the primary's plain
       method. */
    Gauge<int> gg(8);
    int h1 = gg.twice(21); __println("h1 = " + h1);
    int h2 = gg.raw(); __println("h2 = " + h2);

    /* a block-scope class template with a template method (the probe shape). */
    Loc<T>(T q_ = 0) {
        S bump<S>(S s) { S r = s + q_; return r; }
    }
    Loc<int> l(5);
    int k1 = l.bump(2); __println("k1 = " + k1);

    /* a construction-temp receiver. */
    int k2 = TClass<int>(4).smethod(1); __println("k2 = " + k2);

    /* the canon hoisted templates: SClass's own list, reached through the
       LISTLESS qualifier; UClass's unlisted T bound by the REAL qualifier. */
    TClass<>:SClass<int> sobj(7); __println("n1 = " + sobj.s_);
    TClass<float>:UClass<int> uobj(3, 1.5);
    __println("n2 = " + uobj.u_ + " " + uobj.t_);

    /* a second flavor of the same hoisted template — independent memo. */
    TClass<>:SClass<float> sf(2.5); __println("n3 = " + sf.s_);

    /* the INSTANCE-qualified spelling: the qualifier mints TClass<int> and
       the walk reaches ITS per-flavor sub-pattern — sq's type is distinct
       from sf's (`TClass<>:SClass<float>`), the per-qualifier rule. */
    TClass<int>:SClass<float> sq(4.5); __println("nq = " + sq.s_);

    /* hoisted templates with bases: the file-scope base, and the own-list
       instance base with dispatch landing most-derived — both hoisted
       classes are T-independent, so <> reaches them. */
    Host2<>:HD<int> hd(1, 9);
    int hx1 = hd.gv(); __println("hx1 = " + hx1);
    Host2<>:HV<int> hv(3, 2);
    VW<int>^ wp = ^hv;
    int hx2 = wp^.tagv(); __println("hx2 = " + hx2);

    /* DEPTH-2 chains, every qualifier mix: the listless host, listless-
       then-instance, and the full instance chain (whose H<int> flavor was
       minted in warmH — the cross-function memo); plus a template inside a
       PLAIN nested class (P needs the real flavor). */
    warmH();
    Host2<>:H<int> hh(8);
    int hx3 = hh.hv(); __println("hx3 = " + hx3);
    Host2<>:H<int>:D<int> dd1(4);
    int hx4 = dd1.dv(); __println("hx4 = " + hx4);
    Host2<int>:H<int>:D<int> dd2(5);
    int hx5 = dd2.dv(); __println("hx5 = " + hx5);
    Host2<int>:P:E<int> pe(6);
    int hx6 = pe.ev(); __println("hx6 = " + hx6);

    /* depth-2 derived (its base reached through two hoisting levels), the
       depth-2 alias template, the depth-2 member const, and DISTINCT args
       at every level keying independent flavors. */
    Host2<int>:H<int>:G<int> hg(1, 2);
    int hx7 = hg.g2v(); __println("hx7 = " + hx7);
    int zz = 5;
    Host2<int>:H<int>:R2<int> hrp = ^zz;
    __println("hx8 = " + hrp^);
    int hx9 = Host2<int>:H<int>:kH; __println("hx9 = " + hx9);
    Host2<int>:H<int8>:D<float> hdf(1.5);
    __println("hx10 = " + hdf.dv());

    /* the namespace-hosted chain, same and mixed flavors. */
    Spc3:B<int>:C<int> sc(6);
    int hx11 = sc.cv(); __println("hx11 = " + hx11);
    Spc3:B<int8>:C<float> scf(1.5);
    __println("hx12 = " + scf.cv());

    /* methods on a hoisted instance: the bare receiver name; own P from its
       list, the outer T from the qualifier. */
    Host<int>:Pack<int> pk(10, 20);
    int n4 = pk.total(3); __println("n4 = " + n4);
    int n5 = pk.dbl(); __println("n5 = " + n5);

    /* a block-scope host: the sub-pattern lives and dies with the scope. */
    Halo<T>(T unused_ = 0) {
        Duo<D>(D d_ = 0, T e_ = 0) { }
    }
    Halo<int8>:Duo<int> duo(4, 5);
    int n6 = duo.d_ + duo.e_; __println("n6 = " + n6);

    /* the inner list re-using the outer's name: the call's binding wins
       (int8 flavor, int call — 300 fits the CALL's T). */
    Sh<int8> sh(2);
    int p1 = sh.same(300); __println("p1 = " + p1);

    /* hoisted lifecycle: hooks fire per instance object, reverse order. */
    Ho<int8>:Sub<int> hs(10, 3);
    int p2 = hs.sum(); __println("p2 = " + p2);

    /* the qualified construction EXPRESSION fills a plain class's field. */
    Wrap w(Ho<int8>:Sub<int>(7, 1));
    int p3 = w.f_.sum(); __println("p3 = " + p3);

    /* template-method self-recursion inside a flavor. */
    Rec<int> rc;
    int p4 = rc.fact(5); __println("p4 = " + p4);

    /* --- tier 3: templates inside template lists (canon spellings). --- */
    Vector< Vector<int> > vvi;
    vvi;
    Vector< Vector<int >> nrs;
    nrs;

    /* the tight form; construction through the nested spelling; chains. */
    Vector<Vector<int>> tv(Vector<int>(5));
    int q1 = tv.get().get(); __println("q1 = " + q1);
    __println(##type(tv));

    /* three deep: the lexer's `>>` + `>` combination. */
    Vector<Vector<Vector<int>>> deep;
    deep;

    /* a nested use as an alias-template argument, and via the reference. */
    Vector<int> vone(3);
    Rf<Vector<int>> rv = ^vone;
    int q2 = rv^.get(); __println("q2 = " + q2);
    Vector<Vector<int>>^ tp = ^tv;
    int q3 = tp^.get().get(); __println("q3 = " + q3);

    /* a nested use in a function template's explicit type-list. */
    int q4 = idOf<Vector<Vector<int>>>(^tv, 4); __println("q4 = " + q4);

    /* two nested args under one use. */
    Pair<Vector<int>, int8> pn;
    __println(##type(pn));

    /* `>>` and `<` stay expressions where no type gates. */
    int sx = 1;
    int sy = 8;
    int q5 = sy >> sx; __println("q5 = " + q5);
    bool q6 = sx < sy; __println("q6 = " + q6);

    /* hoisted ALIAS templates: own list, T-independent, so <> reaches it;
       ##type keeps the use as written. */
    int rz = 11;
    TClass<>:Ref<int> rp = ^rz;
    int r1 = rp^; __println("r1 = " + r1);
    __println(##type(rp));

    /* an alias target using the outer param UNLISTED: the qualifier binds
       it (T=int; the alias's own slot is free). */
    TClass<int>:Wide<int8> wv = 300; __println("r2 = " + wv);

    /* alias templates inside a template FUNCTION and METHOD body. */
    int r3 = viaAlias(7); __println("r3 = " + r3);
    Meth<int> mm(1);
    int r4 = mm.pick(9); __println("r4 = " + r4);

    /* class + local-function templates inside template fn/method bodies. */
    int r5 = viaLocal(12); __println("r5 = " + r5);
    int r6 = viaFn(12); __println("r6 = " + r6);
    Meth2<int> m2;
    int r7 = m2.meth(4); __println("r7 = " + r7);

    /* the convention on a member template's OUTER param: both S and T bind
       the class Cp; both params arrive `(const Cp)^` behind the spelling. */
    Depth<Cp> dp;
    Cp ca(3);
    Cp cb(4);
    int r8 = dp.both(ca, cb); __println("r8 = " + r8);

    /* arity-only overloading inside a flavor: the count selects per flavor. */
    int r9 = ti.choose(3); __println("r9 = " + r9);
    int r10 = ti.choose(3, 4); __println("r10 = " + r10);

    /* plain beats template inside a flavor: plain for int, template else. */
    CoexF<int> cfx;
    int r11 = cfx.m(1); __println("r11 = " + r11);
    int64 rbig = 9;
    int64 r12 = cfx.m(rbig); __println("r12 = " + r12);

    /* --- the per-flavor member set and the LISTLESS qualifier. --- */

    /* <> reaches the list-independent members: a plain alias, an alias
       template, a plain nested class, a const, an enum (type and member). */
    TClass<>:Integer w1 = 4; __println("w1 = " + w1);
    TClass<>:Type<float> w2 = 1.5; __println("w2 = " + w2);
    TClass<>:HoistI w3(9); __println("w3 = " + w3.i_);
    int w4 = TClass<>:kTc; __println("w4 = " + w4);
    TClass<>:Color w5 = TClass<>:Color:kGreen;
    int w5v = w5; __println("w5 = " + w5v);

    /* a REAL flavor reaches the same members — and the dependent ones. */
    TClass<int>:Integer w6 = 5; __println("w6 = " + w6);
    TClass<int>:RefT w7 = ^w6; __println("w7 = " + w7^);
    TClass<int>:HoistT w8(3); __println("w8 = " + w8.h_);
    int w9 = TClass<float>:kTc; __println("w9 = " + w9);

    /* a flavor's GLOBAL is distinct STORAGE per qualifier: three
       qualifiers, three variables. */
    TClass<>:g_ = 21;
    TClass<int>:g_ = 42;
    TClass<float>:g_ = 63;
    __println("w10 = " + TClass<>:g_ + " " + TClass<int>:g_
              + " " + TClass<float>:g_);

    /* DISTINCT TYPES per qualifier, even with no list dependency —
       ##type spells each side. */
    __println(##type(w3));
    TClass<int>:HoistI w11(1);
    __println(##type(w11));

    /* a construction EXPRESSION through the <> qualifier. */
    int w12 = TClass<>:HoistS<int>(6).s_; __println("w12 = " + w12);

    /* <> composes at depth: a NESTED pattern's own listless flavor. */
    int w13 = Host2<int>:H<>:kH; __println("w13 = " + w13);
    int w14 = Host2<>:H<>:kH; __println("w14 = " + w14);

    return 0;
}
