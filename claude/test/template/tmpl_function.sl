/*
test function templates.

definition:

    return-type function-name < template-list > ( parameter-list ) { body }

template-list is a comma separate list of identifiers used as types.
the return type maybe a type from the template-list.
parameter-list is a normal comma separated list of parameters.
the parameters may use types from the template-list.

usage:

    function-name < type-list > ( argument-list )
    function-name ( argument-list )

the type-list may be inferred from the argument-list.

special handling of template type parameters.
a template function needs to be able to handle primitive and not-primitive types.
a convention of convenience applies here: a parameter of template type is
passed by value if the template type is a primitive.
otherwise it's passed by reference to const.
this convention applies to all template functions regardless of where they are
declared - including: file-scope, nested, namespace, nested in templates, etc.
the template body must rewrite every usage of the argument when it is converted
to a reference.
the mutable qualifier may be applied to a template type parameter.
it is ignored when the template type is not a pointer.

    void template_function<T>(mutable T arg) {
        println(String + arg);
    }

    template_function(42);  -->
    template_function<int>(42);  -->
        void template_function(int arg);

    Class(int a_) { }
    Class obj(37);
    template_function(obj);  -->
    template_function<Class>(obj);  -->
        void template_function(mutable Class^ arg) {
            println(String + arg^);
        }

    alias Tuple = (int,int);
    Tuple t = (1,2);
    template_function(t);  -->
    template_function<Tuple>(t);  -->
        void template_function(mutable Tuple^ arg) {
            println(String + arg^);
        }

this usage is a potential footgun.
T is infered to be Class^, not Class.
arg is type mutable Class^.
its usage in the body is un-munged.

    template_function(^obj);  -->
    template_function<Class^>(^obj);  -->
        void template_function(mutable Class^ arg) {
            println(String + arg);
        }
*/

/*
claude says:

the template body is held unresolved until a call binds the type-list; each
distinct binding is cloned, run through the normal pipeline in the template's
own scope, and memoized (one instance per binding). instance symbols carry the
itanium I..E type-arg encoding.

inference's one job is finding the T bindings: T binds an argument's type
exactly (a literal binds the type a typeless `x = literal` would; conflicting
bindings error). the NOT-template parts of a pattern match by the normal
parameter rules — a t-free subtree imposes no constraint at inference, and on
the way to a T inside a `T^` / `T[]` shape the normal conversions apply
(array decay into the pointee, rvalue materialization into a reference). a
BARE T meeting an array binds the ARRAY type itself (canon tmpl_special.sl —
arrays ride the class/tuple rung of the convention of convenience; an
iterator binding needs an explicit type-list). after binding (inferred or
explicit) the call is an ordinary single-candidate call: widening / implicit
casts / class coercion apply as spec.

overloading: same-name templates coexist iff their ARITY ranges are disjoint
(the argument count selects; no type ranking); a same-name PLAIN function
coexists too — PLAIN BEATS TEMPLATE (the plain set is probed first, widening
included; the template binds what no plain takes; an explicit type-list
forces the template). templates declare anywhere a function does — file
scope, namespaces, blocks.
*/

import string;

T add<T>(T a, T b) {
    return a + b;
}

/* two type parameters; mixed arithmetic inside the instance widens as usual. */
T addu<T, U>(T a, U b) {
    return a + b;
}

/* T in a composite position: pointer to T. */
void bump<T>(mutable T^ p) {
    p^ = p^ + 1;
}

/* T in a composite position: iterator over T. an array argument reaches it via
   the normal decay, explicit or inferred. */
T sum3<T>(T[] p) {
    return p[0] + p[1] + p[2];
}

/* a template calling a template: instantiating triple<int> discovers add<int>. */
T triple<T>(T a) {
    return add(a, add(a, a));
}

/* T appears in no parameter: only callable with an explicit type-list. */
T make<T>() {
    T r = 77;
    return r;
}

/* PLAIN BEATS TEMPLATE: a plain function and a same-name template coexist.
   The plain wins whenever it MATCHES (widening included); the template
   takes what the plain cannot; an explicit type-list forces the template. */
int coex(int a) { return a + 1000; }
T coex<T>(T v) { return v; }

/* ARITY-ONLY OVERLOADING: same-name templates with DISTINCT parameter
   counts coexist; the call's argument count selects. */
T twoWay<T>(T a) { return a + a; }
T twoWay<T>(T a, T b) { return a + b + b; }

/* two templates of the SAME arity may not share a name (no type ranking). */
//-EXPECT-ERROR: may not share its name
//T dup<T>(T v) { return v; }
//U dup<U>(U v) { return v; }

/* overlap counts by RANGE — a default makes [1,2] collide with [1,1]. */
//-EXPECT-ERROR: may not share its name
//T dfl<T>(T v) { return v; }
//T dfl<T>(T v, T w = 3) { return v + w; }

/* a mixed signature: T infers from the first argument; the concrete int64
   parameter is a not-template part — its argument converts normally. */
void zork<T>(T a, int64 b) {
    println(String + a + " " + b);
}

/* T inferred THROUGH a tuple-shaped reference parameter: the `#` tuple is an
   rvalue (materializes into the ref), its char[] slots are the not-template
   parts (normal matching), and T binds through the value-pointer slot. */
void dump<T>( (char[], char[], char[], char[], T^)^ tuple) {
    println(String + tuple^[0] + ":line#: "
        + tuple^[2] + " " + tuple^[3] + " = " + tuple^[4]^);
}

/* a NAMESPACE-scope template: registered in the namespace frame, pattern
   resolved in the types phase, callable qualified (explicit or inferred). */
Space {
    T twice<T>(T v) { return v + v; }
}

/* the EXTERNAL (out-of-line) namespace template definition: relocates into the
   namespace like any external member, then rides the member-template machinery. */
T Space:thrice<T>(T v) { return v + v + v; }

/* an external template may not collide with a declared member. */
//-EXPECT-ERROR: may not share its name
//T Space:twice<T>(T v) { return v; }

/* T bound to a CLASS through reference parameters: the body dispatches the
   class operator and the result returns by value. (Binding a class through a
   BY-VALUE `T` errors like any class value parameter — negative below.) */
Pair(int x_ = 0, int y_ = 0) {
    op+(Pair^ p, Pair^ q) { x_ = p^.x_ + q^.x_; y_ = p^.y_ + q^.y_; }
}
T addc<T>(T^ a, T^ b) {
    return a^ + b^;
}

/* the convention-of-convenience fixtures: a bare-T identity (every binding
   kind flows through it), and `^param` handed to an explicit-ref template. */
T idf<T>(T v) {
    return v;
}
int viaAddr<T>(T p) {
    T r = addc(^p, ^p);
    return r.x_;
}

/* ...a template's body declaring a LOCAL template — the inner's bare-S param
   converts by ITS binding when the outer instantiates ("nested in
   templates" per the canon list). */
T outerId<T>(T v) {
    S innerId<S>(S s) { return s; }
    return innerId(v);
}

/* ...but a CONCRETE class param in a template is NOT of template type — the
   convention never fires for it; the plain rule rejects (negative below). */
T concrete<T>(T a, Pair p) {
    p;
    return a;
}

/* a template declared inside a METHOD body: its instances are nested functions
   in the method's scope, so the body may even read the enclosing object's
   FIELD through the ordinary field machinery. (In the class BODY itself a
   template is a template METHOD — rejected at parse, tmpl_method's item.) */
Tally(int c_ = 0) {
    int bump(int by) {
        T scale<T>(T v) { return v * 2; }
        T plus_c<T>(T v) { return v + c_; }
        c_ = plus_c(scale(by));
        return c_;
    }
}

/* self-recursion: the instance is memoized BEFORE its body resolves, so the
   recursive call inside lands on the very instance being built. */
T fact<T>(T n) {
    if (n <= 1) { return n; }
    return n * fact(n - 1);
}

/* a parameter default inside a template; an omitted argument fills it per
   instance. */
T pad<T>(T a, T b = 3) {
    return a + b;
}

/* the identity shape: value bindings, incl. a (substituted) const. A POINTER
   binding cannot flow through it (canon B — the munge consts the param's
   pointee, the return stays mutable); the read-only deref pins pointer
   bindings instead, and cv_neg_ret pins the rejection. */
T same<T>(T v) { return v; }
int deref1<T>(T p) { return p^; }

/* a bare-T body may not RETURN a pointer binding (the B ruling's negative). */
//-EXPECT-ERROR: Cannot drop 'const'
//S pident<S>(S v) { return v; }
//int cv_neg_ret() { int z = 1; int^ zp = ^z; int^ r = pident(zp); return r^; }

/* one argument binding T twice inside a composite: consistent bindings only. */
T sumpair<T>( (T, T)^ p ) { return p^[0] + p^[1]; }

/* T as the element of a sized-array parameter (the size rides the name). */
T sum3v<T>(T a[3]) { return a[0] + a[1] + a[2]; }

/* the body may ask compile-time questions of T. */
void meta<T>(T v) {
    println(String + "ty = " + ##type(v) + " sz = " + sizeof(T) + " v = " + v);
}

/* an uninstantiated template's body is never checked: nobody calls this, so it
   only has to PARSE (see todo — unchecked uninstantiated bodies). */
void nonsense<T>(T v) {
    v = totally + undefined * names;
}

/* aliases in an explicit type-list canonicalize: add<Integer> IS add<int>. */
alias Integer = int;

/* a plain function, for the type-args-on-a-non-template negative. */
int plain(int v) { return v; }

/* a file-scope function a block-scope template may SHADOW (collision is
   same-scope only; normal lexical shadowing across scopes). */
int shade(int v) { return v; }

/* THE BARE-T PARAM CONVENTION (canon 2026-07-26): a class / tuple binding
   arrives `(const T)^` behind the value spelling; a POINTER binding's
   pointee munges const the same way (`fn<CvP^>` -> `(const CvP)^`).
   `mutable T` (canon 2026-08-18) is the value spelling's explicit opt-out,
   judged on the CONVERTED form: a by-value (primitive) binding has no
   pointer to opt out of, so the keyword is IGNORED; a class / tuple
   binding's convention reference is minted MUTABLE — body writes reach the
   caller; a pointer binding arrives UN-MUNGED (the documented footgun: the
   same param spelling, but T is the pointer and the body sees the pointer).
   Without the keyword nothing changes — params are reference-to-const. */
CvP(int v_ = 0) { }
int cvread<T>(T s) { return s.v_; }
void cvshow<T>(T s) { println(String + ##type(s)); s; }
void cvbump<T>(mutable T^ t) { t^.v_ = t^.v_ + 1; }

/* the `mutable T` fixtures: the identity (legal for EVERY binding kind — the
   pointer binding has no munge, so nothing drops at the return), a writer
   (the class binding's mutable reference reaches the caller), and the
   ##type witness. */
T mut<T>(mutable T v) { return v; }
void mutbump<T>(mutable T c) { c.v_ = c.v_ + 1; }
/* ##type would spell `T` for every mutable binding (no munge rebuilds the
   type, so the pattern's label survives), and sizeof(param) is the pointer
   for both — sizeof(T) is the witness that tells the class binding
   (T = the class) from the pointer binding (T = the pointer, the footgun). */
void mutshow<T>(mutable T s) { println(String + "szT " + sizeof(T)); s; }

/* a bare-T body may not WRITE its param — the convention is const. */
//-EXPECT-ERROR: Cannot write to a const value
//int cv_neg_write<S>(S s) { s.v_ = 9; return s.v_; }
//int cv_neg_use() { CvP c(1); return cv_neg_write(c); }

/* `mutable` on a CONCRETE non-pointer param keeps the plain rejection — the
   value-spelling opt-in (canon 2026-08-18) is for TEMPLATE-typed params only. */
//-EXPECT-ERROR: applies only to a pointer
//void cv_neg_mut<S>(S s, mutable int n) { s; n; }
//void cv_neg_mut_use() { CvP c(1); cv_neg_mut(c, 2); }

/* the CALLER side: a const class cannot flow into `mutable T^`. */
//-EXPECT-ERROR: Cannot pass a const value
//void cv_neg_pass() { const CvP c(1); cvbump(^c); println(String + "" + c.v_); }

/* duplicate type-parameter names. */
//-EXPECT-ERROR: Duplicate type-parameter name
//T bad2<T, T>(T v) { return v; }

/* a template's body IS the template — no forward declaration. */
//-EXPECT-ERROR: A template function must have a body
//T decl_only<T>(T v);

/* a template may not take a class's name either. */
//-EXPECT-ERROR: Duplicate declaration
//Klass(int k_ = 0) { }
//T Klass<T>(T v) { return v; }

/* a runaway recursive instantiation: every call needs a NEW binding (each level
   wraps another `^`), so instantiation never converges — the depth cap reports
   it. (`mutable` keeps the munge from const-qualifying each level differently,
   which would stop the chain early with a cast error instead.) */
//-EXPECT-ERROR: instantiation depth limit
//void deeper<T>(mutable T^ p) { deeper(^p); }
//void start_deeper() { int seed = 1; deeper(^seed); println(String + "seed = " + seed); }

int32 main() {

    int x = add<int>(1, 2); println(String + "x = " + x);
    float f = add<float>(1.9, 2.8); println(String + "f = " + f);

    /* inferred: a literal binds the type a typeless declaration would give. */
    int xi = add(3, 4); println(String + "xi = " + xi);
    float fi = add(1.5, 2.5); println(String + "fi = " + fi);

    /* inferred from typed lvalues; same instance as add<int> above (memoized). */
    int a = 10;
    int b = 20;
    int c = add(a, b); println(String + "c = " + c);

    /* explicit type-list; the argument widens into the bound parameter. */
    int8 i8 = 3;
    int w = add<int>(i8, 4); println(String + "w = " + w);

    /* two type parameters, both inferred. */
    int u = addu(a, i8); println(String + "u = " + u);

    /* pointer position: inferred and explicit. */
    bump(^a); println(String + "a = " + a);
    bump<int>(^b); println(String + "b = " + b);

    /* iterator position via array decay: explicit and inferred. */
    int arr[3] = (1, 2, 3);
    int s3 = sum3<int>(arr); println(String + "s3 = " + s3);
    int s4 = sum3(arr); println(String + "s4 = " + s4);

    /* a template instantiating another template. */
    int t = triple(5); println(String + "t = " + t);

    /* explicit-only: T appears in no parameter. */
    int m = make<int>(); println(String + "m = " + m);

    /* a template declared in a block, like any nested function. */
    T sq<T>(T v) { return v * v; }
    int s = sq(6); println(String + "s = " + s);
    int e = sq<int>(7); println(String + "e = " + e);

    /* a string-literal argument binds T to its ARRAY type since the
       tmpl_special landing (arrays no longer decay at inference); the explicit
       iterator type-list keeps this the mixed-signature iterator case. */
    int za = 62;
    zork<char[]>("za = ", za);

    /* mixed signature: T from the first argument, the second widens normally. */
    zork(a, i8);

    int64 answer = 42;
    dump(#answer);

    /* namespace-scope template, inferred and explicit. */
    int nt = Space:twice(21); println(String + "nt = " + nt);
    int ne = Space:twice<int>(2); println(String + "ne = " + ne);

    /* the externally-defined namespace template, inferred and explicit. */
    int nx = Space:thrice(4); println(String + "nx = " + nx);
    int ny = Space:thrice<int>(5); println(String + "ny = " + ny);

    /* T bound to a class; the instance runs the class operator. */
    Pair p1(3, 4);
    Pair p2(10, 20);
    Pair p3 = addc(^p1, ^p2);
    println(String + "p3 = " + p3.x_ + " " + p3.y_);

    /* templates inside a method body: memoized across calls, field readable. */
    Tally ty(10);
    println(String + "ty1 = " + ty.bump(3));
    println(String + "ty2 = " + ty.bump(4));

    /* self-recursion within one instance. */
    int fa = fact(5); println(String + "fa = " + fa);

    /* a template parameter default: omitted, then supplied. */
    int pd = pad(7); println(String + "pd = " + pd);
    int pe = pad(7, 8); println(String + "pe = " + pe);

    /* T bound to a POINTER type — a READ-ONLY body (canon B, 2026-07-26: a
       bare-T body cannot RETURN a pointer binding; the munge consts the
       pointee while the return type stays mutable — cv_neg_ret below pins
       the rejection); a substituted-const argument through the identity. */
    int^ pi = ^a;
    int qi = deref1(pi); println(String + "qi = " + qi);
    const int ci = 5;
    int cv = same(ci); println(String + "cv = " + cv);

    /* one argument binding T twice, consistently. */
    int sp2 = sumpair((4, 5)); println(String + "sp2 = " + sp2);

    /* T as the element of a sized-array parameter. */
    int s5 = sum3v(arr); println(String + "s5 = " + s5);

    /* compile-time questions of T inside the body. */
    meta(za);

    /* an alias type argument canonicalizes to its underlying instance. */
    int ai = add<Integer>(21, 21); println(String + "ai = " + ai);

    /* an explicit iterator type argument. */
    zork<char[]>("hey", 9);

    /* a block template shadows the same-name file-scope function. */
    T shade<T>(T v) { return v + 100; }
    int sh = shade(1); println(String + "sh = " + sh);

    /* the gate rule's comparison side: parenthesized, `<` is a comparison. */
    bool lt = (a < b); println(String + "lt = " + lt);

    /* a block template CAPTURING an enclosing local: the instance is a nested
       function, so the capture machinery carries `base` in by reference. */
    int base = 1000;
    T offset<T>(T v) { return v + base; }
    int of = offset(7); println(String + "of = " + of);

    /* conflicting exact bindings for T. */
    //-EXPECT-ERROR: Conflicting bindings for template parameter
    //int32 n32 = 1;
    //int8 n8 = 2;
    //int bad = add(n32, n8); println(String + "bad = " + bad);

    /* nothing binds T. */
    //-EXPECT-ERROR: Cannot infer template parameter
    //int nope = make(); println(String + "nope = " + nope);

    /* more explicit type arguments than template parameters. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //int extra = add<int, float>(1, 2); println(String + "extra = " + extra);

    /* too FEW explicit type arguments. */
    //-EXPECT-ERROR: Wrong number of template arguments
    //int few = addu<int>(1, 2); println(String + "few = " + few);

    /* an unknown type in an explicit type-list. */
    //-EXPECT-ERROR: Unknown type
    //int unk = add<Bogus>(1, 2); println(String + "unk = " + unk);

    /* type arguments on a non-template function. */
    //-EXPECT-ERROR: is not a template function
    //int np = plain<int>(1); println(String + "np = " + np);

    /* a structurally unmatchable argument: a pointer against the tuple shape. */
    //-EXPECT-ERROR: does not match the template pattern
    //dump(^answer);

    /* a conflict reached through a composite: both tuple slots bind T. */
    //-EXPECT-ERROR: Conflicting bindings for template parameter
    //int spx = sumpair((1, 2.5)); println(String + "spx = " + spx);

    /* the gate rule: bare `a < b > (c)` IS a template-call shape — the
       comparison reading requires parentheses. */
    //-EXPECT-ERROR: not a function
    //int amb = a < b > (0); println(String + "amb = " + amb);

    /* THE CONVENTION OF CONVENIENCE: T binds a class through the by-value
       spelling — the instance's param is really `(const Pair)^`, the body
       stays generic (`a + b` dispatches op+ through the auto-deref), and the
       result returns by value. */
    Pair px = add(p1, p2); println(String + "px = " + px.x_ + "," + px.y_);

    /* ...the identity shape: class in, class out, explicit and inferred. */
    Pair pi1 = idf<Pair>(p1); println(String + "pi1 = " + pi1.x_ + "," + pi1.y_);
    Pair pi2 = idf(p2); println(String + "pi2 = " + pi2.x_ + "," + pi2.y_);

    /* ...a tuple binding converts the same way. */
    (int, int) tt = (5, 6);
    (int, int) tu = idf(tt);
    println(String + "tu = " + tu[0] + "," + tu[1]);

    /* ...a primitive binds by value; a REFERENCE binding reads through the
       deref pin (canon B — the identity cannot return a pointer binding). */
    int iv = idf(9); println(String + "iv = " + iv);
    int zz = 4;
    int zr = deref1(^zz); println(String + "zr = " + zr);

    /* ...`^param` composes: the addr-of of the auto-deref is the reference. */
    int pxr = viaAddr(p1); println(String + "pxr = " + pxr);

    /* ...a NAMESPACE template converts the same way. */
    Pair ps = Space:twice(p1); println(String + "ps = " + ps.x_ + "," + ps.y_);

    /* ...a BLOCK-scope template too. */
    S blockId<S>(S s) { return s; }
    Pair pb = blockId(p2); println(String + "pb = " + pb.x_ + "," + pb.y_);

    /* ...and a local template INSIDE a template's body. */
    Pair pn = outerId(p1); println(String + "pn = " + pn.x_ + "," + pn.y_);

    /* a CONCRETE class param inside a template keeps the plain rejection —
       the convention is for TEMPLATE-typed params only. */
    //-EXPECT-ERROR: must be a pointer
    //int ncc = concrete(1, p1); println(String + "ncc = " + ncc);

    /* ARITY-ONLY OVERLOADING: the count selects, inferred and explicit; a
       class binding rides the convention through either arity. */
    int w1 = twoWay(4); println(String + "w1 = " + w1);
    int w2 = twoWay(4, 5); println(String + "w2 = " + w2);
    int w3 = twoWay<int>(6); println(String + "w3 = " + w3);
    Pair wp = twoWay(p1, p2); println(String + "wp = " + wp.x_ + "," + wp.y_);

    /* PLAIN BEATS TEMPLATE: the plain matches (widening included), the
       template takes the rest, an explicit list forces the template. */
    int cx1 = coex(1); println(String + "cx1 = " + cx1);
    int8 cs8 = 3;
    int cx2 = coex(cs8); println(String + "cx2 = " + cx2);
    /* the template takes what the plain cannot — a FLOAT binding (a pointer
       binding could not return through the bare-T identity, canon B). */
    float cx3 = coex(2.5); println(String + "cx3 = " + cx3);
    int cx4 = coex<int>(2); println(String + "cx4 = " + cx4);

    /* the bare-T convention: reads through the const ref. A CLASS binding
       AUTO-derefs its uses, so ##type reads the pointee (`const T`); a
       POINTER binding has no rewrite, so ##type shows the munged pointer
       (`(const CvP)^` — the `fn<CvP^>` pin). The sanctioned mutation
       spelling (`mutable T^`) reaches the caller. */
    CvP cvo(4);
    int cv1 = cvread(cvo); println(String + "cv1 = " + cv1);
    cvshow(cvo);                                  // const T
    CvP^ cvp = ^cvo;
    cvshow(cvp);                                  // (const CvP)^
    cvbump(^cvo);
    println(String + "cv2 = " + cvo.v_);                 // 5

    /* `mutable T` (canon 2026-08-18): ignored for a by-value binding; the
       identity returns for EVERY binding kind — including the pointer
       binding the const spelling blocks (no munge here, nothing drops). */
    int mv = mut(11); println(String + "mv = " + mv);
    Pair mp = mut(p1); println(String + "mp = " + mp.x_ + "," + mp.y_);
    (int, int) mtu = mut(tt); println(String + "mt = " + mtu[0] + "," + mtu[1]);
    int^ mzp = mut(^zz); println(String + "mz = " + mzp^);

    /* an ALREADY-const pointer binding flows through the PLAIN identity: T
       carries the const, so the return drops nothing — every spelling. */
    int cix = 5;
    (const int)^ scp = ^cix;
    (const int)^ sc1 = same(scp); println(String + "sc1 = " + sc1^);
    (const int)^ sc2 = same<(const int)^>(scp); println(String + "sc2 = " + sc2^);
    (const int)^ sc3 = same<const int^>(scp); println(String + "sc3 = " + sc3^);

    /* the class binding's MUTABLE reference: the body's write lands in the
       caller's object — the convention's explicit opt-out. */
    CvP mo(7);
    mutbump(mo);
    println(String + "mo = " + mo.v_);
    /* an RVALUE into the mutable-converted param is allowed: it materializes
       into the temp and the mutation discards. */
    mutbump(CvP(3));
    println(String + "mo2 = " + mo.v_);
    /* the ##type contrast with cvshow above: the class binding auto-derefs
       off a MUTABLE reference; the pointer binding is UN-munged (the footgun
       pin), inferred and explicit. */
    mutshow(mo);
    mutshow(^mo);
    mutshow<CvP^>(^mo);

    /* `same<int^>` is STILL illegal — the munge stands on the const spelling. */
    //-EXPECT-ERROR: Cannot drop 'const'
    //int mzz = 4;
    //int^ nr = same<int^>(^mzz); println(String + "nr = " + nr^);

    /* the caller's const cannot flow into the mutable-converted param. */
    //-EXPECT-ERROR: Cannot pass a const value to a 'mutable' parameter
    //const CvP cmo(1);
    //mutbump(cmo);
    //println(String + "cmo = " + cmo.v_);

    return 0;
}
