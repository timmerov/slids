/*
test imported templates defined in another source file.
this is the template source file.
*/

/*
claude says:

the DEFINITIONS file: a function template's body merges onto the header's
declaration; a class template's bodies arrive as a RE-OPEN of the header's
primary opening; a member template's via the out-of-line form. compiling
this file with `--instantiate <build-dir>` IS the post-compile-pre-link
stage: it reads every consumer's .sli, injects their provenance imports,
and instantiates the union of demanded flavors once each, external —
memoized against this file's own uses (viaOwn). private helpers (tick) may
back template bodies: aggregated flavors emit here and link them; only a
consumer's local-type inline instance cannot see them. this file is ALSO a
consumer (of tmpl_lib2 — viaW2), so it dumps its own .sli and tmpl_lib2
compiles after it.
*/

import tmpl_lib;
import tmpl_lib2;

/* PRIVATE to this TU — an instance body may call it, because the instance
   body is always emitted HERE (the aggregation stage is this file's compile). */
void tick() {
}

/* the class template's bodies: a RE-OPEN of the header's primary opening.
   `priv2` is NOT declared in the header: the sibling's flavors have it,
   a consumer's interface does not. */
Vector<T>() {
    void push(T v) {
        tick();
        data_ = data_ + v;
    }
    T sum() { return data_ + extra_; }
    T priv2() { return data_; }
}

/* the sibling sees the undeclared member (compilation pins it). */
int usepriv() {
    Vector<int> t(4, 0);
    return t.priv2();
}

Traced<T>() {
    _() { __println("tr ctor " + t_); }
    ~() { __println("tr dtor " + t_); }
    T get() { return t_; }
}

Box<T>() {
    int has() {
        if (p_ == nullptr) { return 0; }
        return 1;
    }
}

/* the function template's definition merges with the header's declaration. */
T tsum<T>(T a, T b) { return a + b; }

/* the ARITY pair: each definition merges with its same-arity declaration. */
T tpair<T>(T a) { return a + a; }
T tpair<T>(T a, T b) { return a + b + b; }

/* self-contained body: a consumer's local-type instance can inline it. */
T tpick<T>(T a, T b) { return b; }

/* a body on a PRIVATE helper: aggregated flavors emit here and link `tick`
   internally; a consumer's local-type instance has no `tick` and errors. */
T tpock<T>(T a, T b) { tick(); return b; }

/* the identifier flavor: a PRIVATE const read by a template body. */
const int kBias = 100;
T tbias<T>(T v) { return v + kBias; }

/* the template method's body: the out-of-line form; a plain out-of-line
   member beside it. */
T Gauge:scaled<T>(T v) { return v + v; }
int Gauge:base() { return g_ + 1; }

/* the user copy, the virtual slot, the two-parameter class: bodies. */
Acc<T>() {
    op=(Acc^ s) { v_ = s^.v_ + 1; }
    T get() { return v_; }
}

VShape<T>() {
    virtual T vid() { return s_; }
}

TPair<K, V>() {
    K kk() { return k_; }
    V vv() { return v_; }
}

/* the namespace-member templates' bodies, external form. */
T Spc2:nsq<T>(T v) { return v * v; }
T Spc2:nid<T>(T v) { return v; }

/* the pointer-friendly member template (local-type instances inline it). */
T Gauge:tsel<T>(T a, T b) { return b; }

/* the skewed user copy: it NULLS — observable proof the user op ran. */
Ucp<T>() {
    op=(Ucp^ s) { u_ = nullptr; }
    int un() {
        if (u_ == nullptr) { return 0; }
        return 1;
    }
}

/* mixed role: this template SOURCE consumes tmpl_lib2's template... */
int viaW2() {
    Wrap2<int> w(21);
    return w.dub();
}

/* ...and uses one of its own aggregated flavors (memoized against demands). */
int viaOwn() {
    Vector<int> t(2, 3);
    return t.sum();
}

/* a re-open of a header template may not add fields (the header's layout is
   the layout everywhere). */
//-EXPECT-ERROR: cannot add fields
//Vector<T>(T zz_ = 9) { }
