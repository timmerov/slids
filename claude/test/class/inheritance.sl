/*
test class inheritance.

    Base(int a_) { }
    Base : Derived(int a_, int b_) { }

the base class is treated a lot like an unnamed field.
conceptual desugar:

    Derived(Base, int a_, int b_) { }

derived methods and fields may shadow base fields.
base methods and fields are accessible with optional qualifier.

    int Base:method() {
        return a_;
    }
    int Derived:method() {
        b_ = a_ + Base:a_ + Base:method();
        return b_;
    }

pointers to the derived class may be implicityly cast to the base class.
pointers to the base class may be explicitly cast to the derived class.

the ctor/dtor of a derived class will call the ctor/dtor of the base class.
a ctor/dtor will be synthesized for a derived class that needs them because
its base class has them.

ambiguous qualifier chains are compile errors.
there can be name conflicts between inheritance and hoisting.
tbd

the base class is intialized as if it were the first field.

    Derived der = (1, 2, 3);
    Base:a_ is 1.
    Derived:a_ is 2.
    Derived:b_ id 3.

things defined in the base class are accessible in the derived class
with optional qualifier.

    Base() {
        alias Integer = int;
    }
    Base : Derived(Integer x) { }

notes:
no multiple inheritance.
*/

/*
claude says:

LANDED IN STAGES. The base is an unnamed first field: `Base : Derived(...)` desugars
(in grammar) to `Derived(_$base : Base, own fields...)`, so the base is slot 0 and
ALL of layout, construction (a `(1,2)` tuple spreads base-then-derived), the ctor/dtor
hooks (base ctor first, derived dtor first — base is the leading class-typed field),
the needs-fixpoint, and the by-value cycle check reuse the class-typed-field machinery
with no new code.
- STAGE 1 (landed): layout + construction + lifecycle for a derived class.
- STAGE 2a (landed): the derived OPENS the base scope — base statics (alias/const/
  enum) resolve BARE in the derived's field signature and method bodies.
- STAGE 2b (landed): the `Base:` qualifier reframes `self` to slot 0 — `Base:self` is
  the base field tuple, `Base:a_` == `Base:self.a_`, and `Base:method()` calls the base
  method on `self`'s base sub-object. (parse: `self` allowed as a `:` segment; resolve:
  `Base:X` -> `self._$base.X`.)
- STAGE 3 (landed): pointer casts — derived->base IMPLICIT (`A^ a = ^b`), base->derived
  EXPLICIT (`<B^> a`); both offset-0 pointer no-ops. An implicit DOWNcast is rejected.
- STAGE 4 (landed): the chain is TRANSITIVE and members are reachable bare. Construction
  is FLAT (base fields splice in: `C c = (1,2,3)`); a whole ancestor chain's statics,
  FIELDS, and METHODS resolve BARE in a descendant (and via a `GrandBase:` qualifier);
  a base method runs on a derived-typed receiver. A derived same-name method HIDES the
  base's overload set (C++-style name hiding).
- STAGE 5 (landed): a base STATIC (const / alias / enum) is reachable through the
  qualifier (`Base:kConst`) — it defers to the normal qualified-name lookup, NOT a field
  access — which is the disambiguation handle for a shadowed static. A DATA-LESS base
  (only consts / methods) splices 0 fields, so `Derived d = (own...)` constructs flat.
*/

/* STAGE 1 — base = unnamed first field: construction spreads base-then-derived, the
   base ctor runs first and the derived dtor first. */
A(int x_) {
    int gx() { return x_; }
    _() { __println("A:ctor " + x_); }
    ~() { __println("A:dtor " + x_); }
}
A : B(int y_) {
    int by() { return y_; }
    _() { __println("B:ctor " + y_); }
    ~() { __println("B:dtor " + y_); }
}

/* STAGE 2a — the derived opens the base scope: base alias / const / enum resolve BARE
   in the derived's field signature and method bodies. */
P(int p_) {
    alias Num = int;
    const int kBonus = 100;
    enum ( kTag = 9 );
    _() { } ~() { }
}
P : Q(Num q_ = kBonus) {              // base alias `Num` + const `kBonus` in the signature
    _() { } ~() { }
    int total() { return q_ + kBonus + kTag; }   // base const + enum, bare
}

/* COVERAGE — additional working cases. */
int upcast_arg(A^ a) { return a^.gx(); }   // a derived passed where a base ptr is expected

Holder(B inner_) {                         // a derived class AS A FIELD of another class
    int v() { return inner_.by(); }        // ... its own method reached through the field
    _() { } ~() { }
}

A : OvD(int d_) {                          // a derived class with its OWN method overload set
    int g(int x)        { return x; }
    int g(int x, int y) { return x + y; }
    _() { } ~() { }
}

/* STAGE 4 — a TRANSITIVE chain. Inherited members (static / field / method) resolve
   bare across the whole chain, and via a qualifier naming any ancestor. */
G1(int g1_) {
    const int kK = 50;
    int gm() { return g1_; }
    _() { } ~() { }
}
G1 : G2(int g2_) { _() { } ~() { } }
G2 : G3(int g3_) {
    int total() {                          // kK: grandparent static (bare)
        return kK                          // g1_/g2_: grand/parent FIELDS (bare)
             + g1_ + g2_ + g3_             // gm(): grandparent METHOD (bare)
             + gm() + G1:gm() + G1:g1_;    // G1:gm()/G1:g1_: grandparent via qualifier
    }
    _() { } ~() { }
}

/* STAGE 5 — a base STATIC (const / alias / enum) reached through the qualifier, and a
   derived static SHADOWING a same-named base static (qualify to pick which). */
Sa(int a_) {
    const int kBase = 7;
    alias Word = int;
    enum ( kE = 3 );
    _() { } ~() { }
}
Sa : Sd(int d_) {
    const int kBase = 70;                  // shadows Sa:kBase
    int viaQual() {
        Word w = 5;                        // base alias, bare
        return Sa:kBase + Sd:kBase + Sa:kE + w;   // base const / own const / base enum, qualified
    }
    _() { } ~() { }
}

/* STAGE 5 — a DATA-LESS base (only a const + method) splices 0 fields, so the derived
   constructs flat from its OWN fields alone. */
Mb() {
    const int kM = 4;
    int mm() { return kM; }
    _() { } ~() { }
}
Mb : Md(int r_) {
    int info() { return mm() + r_; }       // base method (bare) + own field
    _() { } ~() { }
}

/* COVERAGE — SYNTHESIZED ctor/dtor: a derived that declares NEITHER _() nor ~() but
   whose base has them gets both synthesized to chain the base's. */
Sb(int s_) {
    int sv() { return s_; }
    _() { __println("Sb:ctor " + s_); }
    ~() { __println("Sb:dtor " + s_); }
}
Sb : SynD(int d_) { int dv() { return d_; } }   // no _()/~(): both synthesized

/* COVERAGE — a derived FIELD shadowing a same-named base field: bare resolves to the
   derived's copy, the `Base:` qualifier reaches the base's. */
Fb(int val_) { int fb_get() { return val_; } _(){} ~(){} }
Fb : Fd(int val_) {
    int fd_get()   { return val_; }        // bare -> derived's val_ (shadows base)
    int base_get() { return Fb:val_; }     // qualifier -> base's val_
    _(){} ~(){}
}

int32 main() {

    {
        B b = (1, 2);     // A from 1, B:y_=2
        __println("--");
    }                     // A:ctor 1 / B:ctor 2 / -- / B:dtor 2 / A:dtor 1

    Q q;                  // q_ defaults to kBonus=100
    __println("Q.total = " + q.total());   // 100 + 100 + 9 = 209

    // STAGE 2b — the user's canon: base statics in the signature, the `Base:` qualifier
    // (incl. `Base:self`) in the method.
    Derived der = (1, 10, 11);             // Base:a_=1, Derived:a_=10, b_=11
    __println("der.method = " + der.method());   // 1 + 1 + 1 + 10 + 11 = 24

    // STAGE 3 — pointer casts. (b2 has A:x_=5, B:y_=7.)
    {
        B b2 = (5, 7);
        A^ ap = ^b2;                       // derived -> base, IMPLICIT
        __println("up.gx = " + ap^.gx());  // 5  (base method through a base ptr)
        B^ bp = <B^> ap;                   // base -> derived, EXPLICIT
        __println("down.y_ = " + bp^.y_);  // 7  (derived field through the downcast)
    }                                      // A:ctor 5 / B:ctor 7 / ... / B:dtor 7 / A:dtor 5

    // COVERAGE — additional working cases.
    {
        __println("sizeof B = " + sizeof(B));        // 8 (A:int + B:int)
        B b3 = (5, 7);
        __println("upcast_arg = " + upcast_arg(^b3));  // 5  (derived passed as base ptr)
        Holder h( B(3, 4) );                          // a derived as a class FIELD
        __println("holder.v = " + h.v());             // 4
        OvD od = (1, 2);
        __println("ovd = " + od.g(3) + " " + od.g(3, 4));   // 3 7 (derived's own overload)
        {
            A : L(int l_) { int lv() { return l_; } _(){} ~(){} }   // a LOCAL derived class
            L lc = (1, 9);
            __println("local.lv = " + lc.lv());       // 9
        }
    }

    // STAGE 4 — transitive chain: FLAT construction + bare/qualified inherited members.
    G3 g = (1, 2, 3);                             // flat: g1_=1, g2_=2, g3_=3
    __println("g.total = " + g.total());          // 50 + 1+2+3 + 1 + 1 + 1 = 59
    __println("g.gm = " + g.gm());                // 1 (grandparent method on a G3 receiver)
    {
        G1^ gp = ^g;                              // transitive cast: derived -> GRANDbase
        __println("g.up.gm = " + gp^.gm());       // 1
    }

    // STAGE 5 — base statics via qualifier + data-less base.
    Sd sd = (1, 2);
    __println("sd.viaQual = " + sd.viaQual());    // 7 + 70 + 3 + 5 = 85
    Md md = (7);                                  // data-less base splices 0 fields
    __println("md.info = " + md.info());          // 4 + 7 = 11

    // COVERAGE — copy of a derived (base sub-object copied), and `new Derived[n]`.
    {
        B src = (5, 7);
        B cp = src;
        __println("copy.gx = " + cp.gx());        // 5 (base field copied)
        B[] heap = new B[2];
        delete heap;
        __println("newarr ok");
    }

    // COVERAGE — synthesized ctor/dtor: derived omits both; the base's still run.
    {
        SynD sy = (5, 6);                             // Sb:ctor 5 ... Sb:dtor 5
        __println("syn = " + sy.sv() + " " + sy.dv());   // 5 6
    }

    // COVERAGE — a derived field shadows a same-named base field.
    {
        Fd fd = (10, 20);
        __println("fd shadow = " + fd.fd_get() + " " + fd.base_get());   // 20 10
    }

    // COVERAGE — sizeof a transitive chain, and a SINGLE heap derived (base ctor runs).
    __println("sizeof G3 = " + sizeof(G3));           // 12 (three int fields, flat)
    {
        B^ hp = new B(8, 9);                          // A:ctor 8 / B:ctor 9
        __println("heap.gx = " + hp^.gx());           // 8 (base field through the ptr)
        delete hp;                                    // B:dtor 9 / A:dtor 8
    }

    return 0;
}

/* STAGE 2b canon — base alias/const/enum in the derived signature, the `Base:`
   qualifier (Base:a_ / Base:self.a_ / Base:method()) in the body. */
Base(int a_) {
    alias Integer = int;
    const int kSeven = 7;
    enum ( kEight = 8 );

    _() { __println("Base:ctor: " + a_); }
    ~() { __println("Base:dtor: " + a_); }

    int method() {
        return a_;
    }
}

Base : Derived(Integer a_ = kSeven, int b_ = kEight) {
    _() { __println("Derived:ctor: " + a_ + " " + b_); }
    ~() { __println("Derived:dtor: " + a_ + " " + b_); }

    int method() {
        return Base:a_ + Base:self.a_ + Base:method() + a_ + b_;
    }
}

/* a base -> derived pointer needs an EXPLICIT cast; the implicit form is rejected. */
//-EXPECT-ERROR: Cannot implicitly cast 'A^' to 'B^'
//int32 neg_downcast() {
//    B b = (1, 2);
//    A^ ap = ^b;
//    B^ bp = ap;
//    return bp^.y_;
//}

/* an inheritance CYCLE is an infinite-size by-value embedding (base = first field),
   caught by the same check as a by-value field cycle. */
//-EXPECT-ERROR: contains itself by value
//Cyc1 : Cyc2(int p_) { }
//Cyc2 : Cyc1(int q_) { }

/* a derived same-name method HIDES the base's overload set (C++-style name hiding):
   Hd::f(int,int) hides Hb::f(int), so a one-arg call does NOT fall through to the base. */
//-EXPECT-ERROR: expects 2 arguments, got 1
//Hb(int a_){ int f(int x){ return x; } _(){} ~(){} }
//Hb : Hd(int b_){ int f(int x, int y){ return x + y; } int m(){ return f(3); } _(){} ~(){} }

/* DEFERRED (opened-scope ambiguity): a derived STATIC with the same name as a base
   static is ambiguous BARE — qualify it (`Sa:kBase` / `Sd:kBase`, exercised in
   viaQual above) to disambiguate. */
//-EXPECT-ERROR: ambiguous
//Sa : Samb(int x_) { const int kBase = 99; int m() { return kBase; } _(){} ~(){} }

/* an EXPLICIT cast between UNRELATED classes (neither is a base of the other) is
   rejected — there is no base sub-object offset to reinterpret. */
//-EXPECT-ERROR: Cannot cast 'Ua^' to 'Ub^'
//Ua(int a_) { _(){} ~(){} }
//Ub(int b_) { _(){} ~(){} }
//int32 neg_unrelated_cast() {
//    Ua x = (1);
//    Ua^ ap = ^x;
//    Ub^ bp = <Ub^> ap;
//    return bp^.b_;
//}

/* SIBLINGS (two classes sharing a base) are NOT on each other's chain, so an explicit
   cast between them is rejected too — only up/down a single chain reinterprets. */
//-EXPECT-ERROR: Cannot cast 'Sib1^' to 'Sib2^'
//Wbase(int w_) { _(){} ~(){} }
//Wbase : Sib1(int p_) { _(){} ~(){} }
//Wbase : Sib2(int q_) { _(){} ~(){} }
//int32 neg_sibling_cast() {
//    Sib1 s = (1, 2);
//    Sib1^ p1 = ^s;
//    Sib2^ p2 = <Sib2^> p1;
//    return p2^.q_;
//}

/* a `Base:` qualifier naming a class that is NOT on the chain (baseClassDepth == 0)
   defers to normal qualified lookup, which finds no such member. */
//-EXPECT-ERROR: 'Stranger' has no member 'a_'
//Stranger(int s_) { _(){} ~(){} }
//NotDerived(int a_) { int m() { return Stranger:a_; } _(){} ~(){} }

/* DIRECT self-inheritance is an infinite-size by-value embedding (base = first field),
   caught by the same by-value cycle check as a two-class loop. */
//-EXPECT-ERROR: contains itself by value
//Selfie : Selfie(int q_) { }
