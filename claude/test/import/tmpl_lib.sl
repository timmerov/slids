/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test imported templates defined in another source file.
this is the template source file.
*/

/*
claude says:

the DEFINITIONS file: a function template's body merges onto the header's
declaration; a class template's bodies arrive as a RE-OPEN of the header's
primary opening; a member template's via the out-of-line form. this file
compiles as an ordinary TU and defines only the flavors its own code uses
(viaOwn); the build directory's instantiation stage (`slidsc --instantiate
<build-dir>`) builds every consumer's demands and leaves out what this
object already defines. private helpers (tick) may back template bodies:
every TU hosting an instance keeps the private functions and consts its
bodies reach and emits them internal. this file is ALSO a consumer (of
tmpl_lib2 — viaW2), so it dumps its own .sli into the same pool.
*/

import slids.string;

import tmpl_lib;
alias slids;
import tmpl_lib2;

/* PRIVATE to this TU — an instance body may call it: a TU that hosts the
   instance keeps the private helpers its bodies reach (tick, and tock through
   tick) and emits them internal. */
void tock() {
}
void tick() {
    tock();
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
    _() { println(String + "tr ctor " + t_); }
    ~() { println(String + "tr dtor " + t_); }
    T get() { return t_; }
}

/* the COMPLETING re-open of the header's incomplete template: no trailing
   `...`, so it closes the class and supplies the fields with the bodies. */
Grow<T>(
    T sum_ = 0,
    intptr n_ = 0
) {
    void add(T v) { sum_ = sum_ + v; n_ = n_ + 1; }
    T total() { return sum_; }
    intptr count() { return n_; }
}

/* the completing re-open of `Part`: `sec_` lands after the header's `pub_`,
   private to this file; `tally` proves what an importer's construction left
   in it (the author default). */
Part<T>(
    T sec_ = 40
) {
    T tally() { return pub_ + sec_; }
}

/* the derived header class's body: its own public field, past the hidden one. */
int PDer:zz() { return zed_; }

/* the witness: the same construction spelling that maps `(pub_)` alone for an
   importer maps `(pub_, sec_)` here — privacy is importer-relative. */
int partpriv() {
    Part<int> q(7, 8);
    return q.sec_;
}

/* Sack's completion: a private pointer field; the bodies are self-contained
   (no TU-private helper), so a local-type flavor can inline them. */
Sack<T>(
    T^ hold_ = nullptr
) {
    /* the munge consts a class binding's pointee; store the mutable view
       (the vector.sl next() idiom). */
    void put(T^ v) { hold_ = <T^> <mutable> v; }
    int got() {
        if (hold_ == nullptr) { return 0; }
        return 1;
    }
}

Box<T>() {
    int has() {
        if (p_ == nullptr) { return 0; }
        return 1;
    }
}

Rank<T>() {
    int has() {
        if (p_ == nullptr) { return 0; }
        return 1;
    }
    bool lt(Rank^ o) {
        return p_^ < o^.p_^;
    }
}

/* the function template's definition merges with the header's declaration. */
T tsum<T>(T a, T b) { return a + b; }

/* the ARITY pair: each definition merges with its same-arity declaration. */
T tpair<T>(T a) { return a + a; }
T tpair<T>(T a, T b) { return a + b + b; }

/* self-contained body: a consumer's local-type instance can inline it. */
T tpick<T>(T a, T b) { return b; }

/* the POINTER-demand carrier: read-only, so a pointer binding's const-munged
   params never flow into the mutable return (canon B — tpick serves VALUE
   bindings only now). */
bool tdiff<T>(T a, T b) { return a != b; }

/* the iterator-arithmetic pair: minimal bodies — the whole test is WHERE
   they emit (here, where an opaque T has no layout). The munge consts the
   params' pointees, so the mutable return reinterprets first, the same
   idiom as vector.sl's next(). */
T[] tstep<T>(T[] p, intptr n) {
    iter = <T[]> <mutable> p;
    return iter + n;
}
intptr tgap<T>(T[] a, T[] b) { return b - a; }

/* a body on a PRIVATE helper: every TU hosting a flavor (here, and a
   consumer's local-type instance) gets its own internal `tick`. */
T tpock<T>(T a, T b) { tick(); return b; }

/* the identifier flavor: a PRIVATE const read by a template body. */
const int kBias = 100;
T tbias<T>(T v) { return v + kBias; }

/* the global flavor: a PRIVATE mutable global written by a template body. */
int gCount = 0;
T tcount<T>(T v) { int was = gCount; gCount = was + 1; return v; }

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

/* the arms' bodies: a plain re-open joins the catch-all, `T=int` joins that arm
   exactly, and the `<int8|bool>` filter defines the header filter's member. */
Spec<T>() { int kind() { return 0; } }
Spec<T=int>() {
    int kind() { return 1; }
    int both() { return z_ + w_; }
}
Spec<T=<int8|bool>>() { int pick() { return 8; } }

/* a re-open of a header template may not add fields (the header's layout is
   the layout everywhere). */
//-EXPECT-ERROR: cannot add fields
//Vector<T>(T zz_ = 9) { }

/* nor may the template source add an ARM the header does not declare — every TU
   must see one family. */
//-EXPECT-ERROR: An arm of class template 'Spec' must be declared in its header.
//Spec<T=float32>(float32 f_) { }

/* public and private classes */
Hybrid(int b = 37) {
    _() {} ~() {}
    void print() {
        println(String + "a=" + a + " b=" + b);
    }
}

/* the completer owns the private tail, so HERE the list reaches it: `b` is nameable and
   initializer-mappable in this file alone, and the site fills both fields before the
   hooks. An importer's same spelling is an arity error (tmpl_test's hybc). */
int hybpriv() {
    Hybrid h(1, 2);
    return h.a * 100 + h.b;
}
