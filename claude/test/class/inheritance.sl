/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test class inheritance.

    Base(int a_) { }
    Base : Derived(int a_, int b_) { }

the base class is treated a lot like an unnamed field.
conceptual desugar:

    Derived(Base, int a_, int b_) { }

derived methods and fields may shadow base fields.
base methods are accessed using base class name and colon syntax.
base fields are accessed by self dot base class syntax.

    int Base:method() {
        return a_;
    }
    int Derived:method() {
        b_ = a_ + self.Base.a_ + Base:method() + self.Base:method();
        return b_;
    }

ancestor methods can be accessed with a single qualifier.
chainging them is optional.
ancestor fields must be accessed by walking the heirarchy.

    Base : Mid(int a_) { }
    Mid : Derived(int a) {
        void foo() {
            a = a_;
            a = self.a_;
            mid_a = self.Mid.a_;
            base_a = self.Mid.Base.a_;

            /* foo declarations for other classes implied. */
            foo();
            Derived:foo();
            Mid:foo();
            Base:foo();
            Base:Mid:Derived:foo();
            Derived:Mid:Base:foo(); // error, wrong order.
        }
    }

pointers to the derived class may be implicityly cast to the base class.
pointers to the base class may be explicitly cast to the derived class.

the ctor/dtor of a derived class will call the ctor/dtor of the base class.
a ctor/dtor will be synthesized for a derived class that needs them because
its base class has them.

the base class is intialized as if it were the first field.

    Derived der(1, 2, 3);
    der.Base.a_ is 1.
    der.a_ is 2.
    der.b_ is 3.

    within a method:
    self.Base.a_ is 1.
    self.a_ is 2.
    self.b_ is 3.

access the base class of a derived object.

    Base base = der.Base;
    der.Base.a_ = 4;
    der.Base = Base(5);

things defined in the base class are accessible in the derived class
with optional qualifier.

    Base() {
        alias Integer = int;
    }
    Base : Derived(Integer x) { }

ancestor qualifiers (like namespace or template type list) are not used
to access ancestor fields and methods.
for methods: nearest matching ancestor wins when multiple ancestors have
the same non-qualified name.

    Space {
        Base(int a_) { }
    }
    Space:Base : Derived(int a_) {
        void foo() {
            a_ = self.Base.a_;
            error = self.Space:Base.a_;
        }
    }
    Home {
        Space:Base : Base<T>(T a_) { }
    }
    Home:Base<int> : Derived(int a_) {
        void foo() {
            home_base_a = self.Base.a_;
            space_base_a = self.Base.Base.a_;

            foo();  // Derived:foo
            Base:foo();  // Home:Base:foo
            self.Base.Base:foo();  // Space:Base:foo
        }
    }

ambiguous qualifier chains are compile errors.
there can be name conflicts between inheritance and hoisting.

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
- STAGE 2b (landed; REWORKED 2026-11-17): `.` selects, `:` changes context. `obj.Base`
  (the DIRECT base's bare name) IS the slot-0 `_$base` sub-object — classify's kFieldExpr
  arm maps the name — so an ancestor FIELD is reached by walking (`self.Cm.Cb.a_`), and
  the base object is read / written whole (`der.Base = Base(5)` through Base's op=).
  A METHOD qualifier — `X:m()` (self optional), `self.X:m()`, `obj.X:m()`, a root-first
  chain `A:B:X:m()` — calls X's method directly: resolve turns the bare form into
  `self.X:m()` when X is the class or an ancestor (tryResolveBaseQualifier), the grammar
  parses `recv.X:m()` (parseQualifiedMember), and classify (qualifiedMethodReceiver)
  finds X by BARE name on the receiver's ancestry (nearest wins, may skip), validates the
  chain, hops the receiver to X's sub-object, and marks the bypass. The old reframes —
  `Base:a_` (a field), `Base:self`, `Base:self.m()` — are gone; they fall to the normal
  qualified lookup's errors.
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
import slids.string;
alias slids;

A(int x_) {
    int gx() { return x_; }
    _() { println(String + "A:ctor " + x_); }
    ~() { println(String + "A:dtor " + x_); }
}
A : B(int y_) {
    int by() { return y_; }
    _() { println(String + "B:ctor " + y_); }
    ~() { println(String + "B:dtor " + y_); }
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
             + gm() + G1:gm() + self.G2.G1.g1_;  // G1:gm(): grandparent method via qualifier; self.G2.G1.g1_: field by walking
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

/* COVERAGE — a derived that adds NO fields is the SAME SIZE as its base: the only
   shape whose ITERATOR demotes to the base implicitly (cast.sl states it as an iff —
   an iterator strides by its pointee, so a bigger derived would land mid-object). */
Ib(int iv_) {
    int ival() { return iv_; }
}
Ib : If() {
    int idbl() { return ival() * 2; }
}

/* COVERAGE — SYNTHESIZED ctor/dtor: a derived that declares NEITHER _() nor ~() but
   whose base has them gets both synthesized to chain the base's. */
Sb(int s_) {
    int sv() { return s_; }
    _() { println(String + "Sb:ctor " + s_); }
    ~() { println(String + "Sb:dtor " + s_); }
}
Sb : SynD(int d_) { int dv() { return d_; } }   // no _()/~(): both synthesized

/* COVERAGE — a derived FIELD shadowing a same-named base field: bare resolves to the
   derived's copy, `self.Base.` reaches the base's. */
Fb(int val_) { int fb_get() { return val_; } _(){} ~(){} }
Fb : Fd(int val_) {
    int fd_get()   { return val_; }        // bare -> derived's val_ (shadows base)
    int base_get() { return self.Fb.val_; }   // self.Base. -> base's val_
    _(){} ~(){}
}

/* COVERAGE — ENUM CONTEXT through an INHERITED field: a store / comparison against a
   base's enum-typed field, reached through a derived (or grand-derived) receiver,
   reads the bare member in the field's enum. */
Eb(Shade shade_ = kDim) {
    enum Shade (kDim, kBright);
}
Eb : Ed(int ed_ = 0) { }
Ed : Ee(int ee_ = 0) { }

/* CANON — ancestor methods through a single qualifier (any ancestor; a chain, root first,
   is optional), ancestor FIELDS by walking the hierarchy one level per `.`. */
Cb(int a_) {
    void foo() { println(String + "Cb:foo " + a_); }
}
Cb : Cm(int a_) {
    void foo() { println(String + "Cm:foo " + a_); }
}
Cm : Cd(int a) {
    void foo() { println(String + "Cd:foo " + a); }
    void calls() {
        println(String + "a_ = " + a_ + ", self.a_ = " + self.a_);   // nearest: Cm's
        mid_a = self.Cm.a_;
        base_a = self.Cm.Cb.a_;
        println(String + "self.Cm.a_ = " + mid_a + ", self.Cm.Cb.a_ = " + base_a);
        foo();               // Cd:foo
        Cd:foo();            // own class
        Cm:foo();
        Cb:foo();            // skips Cm
        Cb:Cm:Cd:foo();      // a chain, root first -> Cd:foo
        self.Cb:foo();       // self is optional
    }
}

/* CANON — ancestors are named by their BARE name (no namespace qualifier, no template
   type list); for methods the NEAREST ancestor of that name wins.
   claude says: the canon derives straight from `Space:Base` / `Home:Base<int>`. Two
   out-of-scope limits shape this version: a qualified base in a derived-class
   declaration does not parse (the "ambiguous qualifier chain" item), so the hierarchy
   is built through aliases; and a class `Base` deriving DIRECTLY from another `Base`
   reports a by-value cycle (the "inheritance and hoisting" name conflict), so a middle
   class `Mid2` sits between the two `Base`s. */
Space {
    Base(int a_) {
        void foo() { println(String + "Space:Base:foo " + a_); }
    }
}
alias SpaceBase = Space:Base;
SpaceBase : SpDer(int a_) {
    void foo() {
        println(String + "SpDer: self.Base.a_ = " + self.Base.a_ + ", a_ = " + a_);
    }
}
SpaceBase : Mid2(int m_) { }
Home {
    Mid2 : Base<T>(T a_) {
        void foo() { println(String + "Home:Base:foo " + a_); }
    }
}
alias HomeBaseInt = Home:Base<int>;
HomeBaseInt : HmDer(int a_) {
    void foo() { println(String + "HmDer:foo " + a_); }
    void calls() {
        home_base_a = self.Base.a_;
        space_base_a = self.Base.Mid2.Base.a_;
        println(String + "self.Base.a_ = " + home_base_a
                + ", self.Base.Mid2.Base.a_ = " + space_base_a);
        foo();                        // HmDer:foo
        Base:foo();                   // the nearest Base: Home:Base:foo
        self.Base.Mid2.Base:foo();    // walk, then qualify: Space:Base:foo
        Base:Mid2:Base:foo();         // a chain, root first: ends at Home:Base:foo
    }
}

int32 main() {

    {
        B b = (1, 2);     // A from 1, B:y_=2
        println(String + "--");
    }                     // A:ctor 1 / B:ctor 2 / -- / B:dtor 2 / A:dtor 1

    Q q;                  // q_ defaults to kBonus=100
    println(String + "Q.total = " + q.total());   // 100 + 100 + 9 = 209

    // STAGE 2b — the user's canon: base statics in the signature, `self.Base.a_` and the
    // `Base:` method qualifier in the method.
    Derived der = (1, 10, 11);             // der.Base.a_=1, der.a_=10, der.b_=11
    println(String + "der.method = " + der.method());   // 1 + 1 + 1 + 10 + 11 = 24

    // STAGE 3 — pointer casts. (b2 has A:x_=5, B:y_=7.)
    {
        B b2 = (5, 7);
        A^ ap = ^b2;                       // derived -> base, IMPLICIT
        println(String + "up.gx = " + ap^.gx());  // 5  (base method through a base ptr)
        B^ bp = <B^> ap;                   // base -> derived, EXPLICIT
        println(String + "down.y_ = " + bp^.y_);  // 7  (derived field through the downcast)
    }                                      // A:ctor 5 / B:ctor 7 / ... / B:dtor 7 / A:dtor 5

    // COVERAGE — additional working cases.
    {
        println(String + "sizeof B = " + sizeof(B));        // 8 (A:int + B:int)
        B b3 = (5, 7);
        println(String + "upcast_arg = " + upcast_arg(^b3));  // 5  (derived passed as base ptr)
        Holder h( B(3, 4) );                          // a derived as a class FIELD
        println(String + "holder.v = " + h.v());             // 4
        OvD od = (1, 2);
        println(String + "ovd = " + od.g(3) + " " + od.g(3, 4));   // 3 7 (derived's own overload)
        {
            A : L(int l_) { int lv() { return l_; } _(){} ~(){} }   // a LOCAL derived class
            L lc = (1, 9);
            println(String + "local.lv = " + lc.lv());       // 9
        }
    }

    // STAGE 4 — transitive chain: FLAT construction + bare/qualified inherited members.
    G3 g = (1, 2, 3);                             // flat: g1_=1, g2_=2, g3_=3
    println(String + "g.total = " + g.total());          // 50 + 1+2+3 + 1 + 1 + 1 = 59
    println(String + "g.gm = " + g.gm());                // 1 (grandparent method on a G3 receiver)
    {
        G1^ gp = ^g;                              // transitive cast: derived -> GRANDbase
        println(String + "g.up.gm = " + gp^.gm());       // 1
    }

    // STAGE 5 — base statics via qualifier + data-less base.
    Sd sd = (1, 2);
    println(String + "sd.viaQual = " + sd.viaQual());    // 7 + 70 + 3 + 5 = 85
    Md md = (7);                                  // data-less base splices 0 fields
    println(String + "md.info = " + md.info());          // 4 + 7 = 11

    // COVERAGE — copy of a derived (base sub-object copied), and `new Derived[n]`.
    {
        B src = (5, 7);
        B cp = src;
        println(String + "copy.gx = " + cp.gx());        // 5 (base field copied)
        B[] heap = new B[2];
        delete heap;
        println(String + "newarr ok");
    }

    // COVERAGE — synthesized ctor/dtor: derived omits both; the base's still run.
    {
        SynD sy = (5, 6);                             // Sb:ctor 5 ... Sb:dtor 5
        println(String + "syn = " + sy.sv() + " " + sy.dv());   // 5 6
    }

    // COVERAGE — a derived field shadows a same-named base field.
    {
        Fd fd = (10, 20);
        println(String + "fd shadow = " + fd.fd_get() + " " + fd.base_get());   // 20 10
    }

    // COVERAGE — the two pointer kinds answer the derived->base demotion differently
    // (cast.sl's iff). A REFERENCE names one object and always demotes. An ITERATOR
    // strides by its pointee, so it demotes only between SAME-SIZE classes: If adds no
    // fields to Ib, so the base iterator walks the derived array correctly.
    {
        If farr[2] = (3, 4);
        Ib^ fref = ^farr[0];                          // reference: always
        println(String + "ref demote = " + fref^.ival());    // 3
        Ib[] fit = ^farr[0];                          // iterator: same size, so implicit
        println(String + "iter demote = " + fit[0].ival() + " " + fit[1].ival());   // 3 4
    }

    // COVERAGE — sizeof a transitive chain, and a SINGLE heap derived (base ctor runs).
    println(String + "sizeof G3 = " + sizeof(G3));           // 12 (three int fields, flat)
    {
        B^ hp = new B(8, 9);                          // A:ctor 8 / B:ctor 9
        println(String + "heap.gx = " + hp^.gx());           // 8 (base field through the ptr)
        delete hp;                                    // B:dtor 9 / A:dtor 8
    }

    // COVERAGE — enum context through an inherited field.
    {
        ep = new Ed;
        ep^.shade_ = kBright;                         // base field via derived ptr
        println(String + "ed bright = " + (ep^.shade_ == kBright));   // true
        Ee^ gp = new Ee;
        gp^.shade_ = kBright;                         // grandbase field
        gp^.shade_ = kDim;
        println(String + "ee dim = " + (gp^.shade_ == kDim));         // true
        delete gp;
        delete ep;
    }

    // CANON — the base class is initialized as if it were the first field; a derived
    // object's BASE OBJECT is `der.Base`: read whole, written field-wise, written whole.
    println(String + "== base object ==");
    {
        Derived d2(1, 2, 3);
        println(String + "d2.Base.a_ = " + d2.Base.a_ + ", d2.a_ = " + d2.a_
                + ", d2.b_ = " + d2.b_);                         // 1 2 3
        Base base = d2.Base;
        d2.Base.a_ = 4;
        println(String + "base.a_ = " + base.a_ + ", d2.Base.a_ = " + d2.Base.a_);   // 1 4
        d2.Base = Base(5);
        println(String + "d2.Base.a_ = " + d2.Base.a_);         // 5
        println(String + "-- end base object --");
    }

    // CANON — ancestor methods by qualifier, ancestor fields by walking.
    println(String + "== ancestor chain ==");
    {
        Cd cd = (1, 2, 3);                        // Cb:a_=1, Cm:a_=2, Cd:a=3
        cd.calls();
    }

    // CANON — bare ancestor names; the nearest ancestor of a name wins.
    println(String + "== bare ancestor names ==");
    {
        SpDer sp = (6, 7);
        sp.foo();
        HmDer hm = (8, 0, 9, 10);    // Space:Base:a_=8, Mid2:m_=0, Home:Base:a_=9, a_=10
        hm.calls();
    }

    return 0;
}

/* STAGE 2b canon — base alias/const/enum in the derived signature; in the body a base
   FIELD through `self.Base.`, a base METHOD through `Base:` (self optional). */
Base(int a_) {
    alias Integer = int;
    const int kSeven = 7;
    enum ( kEight = 8 );

    _() { println(String + "Base:ctor: " + a_); }
    ~() { println(String + "Base:dtor: " + a_); }

    int method() {
        return a_;
    }
}

Base : Derived(Integer a_ = kSeven, int b_ = kEight) {
    _() { println(String + "Derived:ctor: " + a_ + " " + b_); }
    ~() { println(String + "Derived:dtor: " + a_ + " " + b_); }

    int method() {
        return a_ + self.Base.a_ + Base:method() + self.Base:method() + b_;
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

/* a derived ITERATOR demotes to the base only when the two are the SAME SIZE. Xd adds
   a field, so a base iterator would stride past the wrong objects — rejected (the
   REFERENCE form of the same edge stays implicit). */
//-EXPECT-ERROR: Cannot implicitly cast 'Xd[]' to 'Xb[]'
//Xb(int xb_) { }
//Xb : Xd(int xd_) { }
//int32 neg_iter_size() {
//    Xd arr[2];
//    Xb[] it = ^arr[0];
//    return it[0].xb_;
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

/* an ancestor-qualifier CHAIN names the root ancestor FIRST — the reverse is an error. */
//-EXPECT-ERROR: does not follow the ancestry
//Cd : Wrong(int w_) {
//    void bad() { Wrong:Cd:Cm:Cb:foo(); }
//}

/* `.` walks ONE level: an ancestor further up is not a member of the object. */
//-EXPECT-ERROR: has no field 'Cb'
//Cm : Skip(int s_) {
//    int bad() { return self.Cb.a_; }
//}

/* a base FIELD is reached through its object (`self.Base.a_`); `Base:a_` is not a field. */
//-EXPECT-ERROR: 'Cb' has no member 'a_'
//Cb : OldQual(int q_) {
//    int bad() { return Cb:a_; }
//}

/* ancestor qualifiers are not used: the base name after `.` is bare, and a qualified
   member after `.` is a method call. */
//-EXPECT-ERROR: A qualified member after '.' must be a method call
//SpaceBase : QualDot(int q_) {
//    int bad() { return self.Space:Base.a_; }
//}

/* a qualifier naming a class that is not the object's class or an ancestor. */
//-EXPECT-ERROR: is not 'Cd' or one of its ancestors
//int32 neg_not_ancestor() {
//    Cd cd = (1, 2, 3);
//    cd.Base:method();
//    return 0;
//}

/* DIRECT self-inheritance is an infinite-size by-value embedding (base = first field),
   caught by the same by-value cycle check as a two-class loop. */
//-EXPECT-ERROR: contains itself by value
//Selfie : Selfie(int q_) { }
