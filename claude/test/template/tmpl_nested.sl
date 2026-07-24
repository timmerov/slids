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

a HOISTED (nested) class template registers a SUB-PATTERN under the
qualified spelling itself — "TClass:SClass" is the entry name, found whole
by the template-use lookup before any namespace walk. its template list is
SELF-CONTAINED: the inner lists every parameter it uses (re-listing an
outer's name if wanted — the binding surface is the inner list alone), so
an instance is keyed by one arg list, needs no outer flavor, and splices
into the outer's host list as an ordinary class. the outer contributes
only the name qualifier: no outer template list in the spelling (`TClass:`,
never `TClass<int>:`). inside its own bodies the bare inner name means the
instance (matched by the pattern's spelled name — the entry name carries
the ':'). the bare inner name resolves nowhere outside.

still rejected: a template method in a HEADER-owned class template (the
cross-TU bundle), nested anything in a header-owned template, and the
out-of-line form targeting a template.
*/

/* the canon workhorse: a template method mixing the outer T and its own S,
   and the two canon HOISTED class templates — SClass's list is its own alone;
   UClass re-lists the outer's T in its own list (the only way to use it). */
TClass<T>(T t_) {
    S smethod<S>(S s) {
        __println("t=" + t_ + " s=" + s);
        return s;
    }
    SClass<S>(S s_) { }
    UClass<U, T>(U u_, T t_) { }
    /* hoisted ALIAS templates ride the same sub-pattern rules: own list,
       bare-host qualifier, re-list an outer param to use it. */
    alias Ref<S> = S^;
    alias Wide<S, T> = T;
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

/* a hoisted template with METHODS: its own bare name is the receiver type,
   and both its params (own P + re-listed T) bind from its one list. */
Host<T>(T h_ = 0) {
    Pack<P, T>(P p_ = 0, T q_ = 0) {
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

/* lifecycle hooks in a hoisted template; a hoisted instance as a plain
   class's FIELD, filled by the qualified construction EXPRESSION. */
Ho<T>(T unused_ = 0) {
    Sub<S, T>(S s_ = 0, T t_ = 0) {
        _() { __println("sub ctor " + s_); }
        ~() { __println("sub dtor " + s_); }
        S sum() { return s_ + t_; }
    }
}
Wrap(Ho:Sub<int, int8> f_) { }

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

/* a template method owns its name inside a class template too — the plain
   rule re-fires per flavor, at the instance's registration. */
//-EXPECT-ERROR: may not share its name
//BadN<T>(T a_ = 0) {
//    int m(int v) { return v; }
//    U m<U>(U v) { return v; }
//}
//int badn_use() { BadN<int> b; return b.a_; }

/* a hoisted template's bare name resolves nowhere outside its host. */
//-EXPECT-ERROR: Unknown type
//int bads() { SClass<int> s; s; return 0; }

/* the qualifier is the bare host name — never the host with a type-list. */
//-EXPECT-ERROR: Expected
//int badq() { TClass<int>:SClass<float> s; s; return 0; }

/* a hoisted template's list is SELF-CONTAINED: an outer param it does not
   re-list is simply not in scope. */
//-EXPECT-ERROR: Unknown type
//Outc<T>(T t_ = 0) {
//    BadU<S>(S s_ = 0, T oops_ = 0) { }
//}
//int badu() { Outc:BadU<int> b; b; return 0; }

/* a nested template method checks its explicit arity like any template. */
//-EXPECT-ERROR: Wrong number of template arguments
//int bada() { TClass<int> t(1); return t.smethod<int, int>(5); }

/* a cross-family mix inside a flavor's method instance: the message spells
   each ALIAS as label=target, so the bound types are visible through T/X. */
//-EXPECT-ERROR: No common type for 'T=float' and 'X=int'
//float badm() { Box<float> bb(0.5); return bb.viaT(2); }

/* an alias target may not use an UNLISTED outer param — the hoisted list is
   self-contained for aliases too. */
//-EXPECT-ERROR: Unknown type
//BadAl<T>(T t_ = 0) {
//    alias Leak<S> = T^;
//}
//int badal() { BadAl:Leak<int> p = nullptr; p; return 0; }

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

    /* the canon hoisted templates: SClass's own list; UClass re-lists T. */
    TClass:SClass<int> sobj(7); __println("n1 = " + sobj.s_);
    TClass:UClass<int, float> uobj(3, 1.5);
    __println("n2 = " + uobj.u_ + " " + uobj.t_);

    /* a second flavor of the same hoisted template — independent memo. */
    TClass:SClass<float> sf(2.5); __println("n3 = " + sf.s_);

    /* methods on a hoisted instance: the bare receiver name, both params
       from the one self-contained list. */
    Host:Pack<int, int> pk(10, 20);
    int n4 = pk.total(3); __println("n4 = " + n4);
    int n5 = pk.dbl(); __println("n5 = " + n5);

    /* a block-scope host: the sub-pattern lives and dies with the scope. */
    Halo<T>(T unused_ = 0) {
        Duo<D, T>(D d_ = 0, T e_ = 0) { }
    }
    Halo:Duo<int, int8> duo(4, 5);
    int n6 = duo.d_ + duo.e_; __println("n6 = " + n6);

    /* the inner list re-using the outer's name: the call's binding wins
       (int8 flavor, int call — 300 fits the CALL's T). */
    Sh<int8> sh(2);
    int p1 = sh.same(300); __println("p1 = " + p1);

    /* hoisted lifecycle: hooks fire per instance object, reverse order. */
    Ho:Sub<int, int8> hs(10, 3);
    int p2 = hs.sum(); __println("p2 = " + p2);

    /* the qualified construction EXPRESSION fills a plain class's field. */
    Wrap w(Ho:Sub<int, int8>(7, 1));
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

    /* hoisted ALIAS templates: own list; ##type keeps the use as written. */
    int rz = 11;
    TClass:Ref<int> rp = ^rz;
    int r1 = rp^; __println("r1 = " + r1);
    __println(##type(rp));

    /* an alias re-listing the outer's param: its own second slot binds it. */
    TClass:Wide<int8, int> wv = 300; __println("r2 = " + wv);

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

    return 0;
}
