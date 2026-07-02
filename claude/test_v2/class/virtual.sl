/*
test virtual classes.

virtual classes have one or more virtual methods.
the compiler synthesizes an empty virtual dtor for a virtual class that
does not explicity define its dtor.
the explicitly declared dtor of a virtual class must be virtual.
ancestor classes of a virtual class must be virtual.
virtual methods may be pure - syntax below.
a class with a pure virtual method may not be instantiated.
multiple inheritance for virtual classes is not supported.
virtual classes may be re-opened.
all virtual methods must be declared in the original class declaration.
virtual methods may not be added to a re-opened virtual class.
virtual methods may be overloaded.
virtual methods may have optional parameters with default parametes.
ambiguity is a compiler error.

    PureVirtual(int x) {
        _() {}
        virtual ~() {}

        virtual void pure() = delete;
        virtual void method() { }
    }

    PureVirtual : Virtual(int y) {
        /* synthesized ctor and virtual dtor. */

        virtual void pure() { }
        virtual void method() { }
    }

    PureVirtual error;
    Virtual virt;

virtual methods may not be shadowed by non-virtual methods.
non-virtual methods may not be shadowed by virtual methods.
the virtual keyword is required when overriding virtual methods.

methods may bypass the dispatch table using qualifiers or self.

    Virtual ByPass(int z) {
        virtual void method() {
            PureVirtual:method();
            Virtual:method();
            ByPass:method();

            PureVirtual:self.method();
            Virtual:self.method();
            ByPass:self.method();
            self.method();
        }
    }

during the construction of a derived virtual class, when the base class ctor is called,
the dispatch table is set for the base class, not the derived class.
during the destruction of a derived virtual class, when the base class dtor is called,
the dispatch table is set for the base class, not the derived class.
it must be this way.
otherwise undefined behavior can occur - including crashes.
*/

/*
claude says:

Landing virtual classes in stages (each stage lands with its tests):
  1. PARSE — `virtual` on a method / the destructor; `= delete` pure syntax. (landed)
  2. LAYOUT — a hidden vtable pointer `_$vptr` at OFFSET 0 (C++ ABI), so a virtual class
     grows by one pointer; a root class carries it, a derived class inherits it via
     `_$base`. Construction skips it; the field is real storage. (landed)
  3. vtable + dynamic dispatch — emit the per-class vtable global, the ctor stamps it at
     offset 0, and a self. / obj. / ptr^ call loads it and calls indirect; a qualified
     `Base:method()` stays a STATIC bypass. (this stage — the vtable emission + ctor stamp
     moved here from stage 2: the slot map is defined by what dispatch needs, so it is
     built once, with dispatch, rather than twice.)
  4. virtual dtor — the destructor gets vtable slot 0 and `delete base_ptr` dispatches the
     most-derived one; scope exit of a known type stays static. (this stage)
  5. semantic checks + synthesis — base-must-be-virtual, dtor-must-be-virtual, override
     rules, pure ⇒ abstract (not instantiable), and treating a virtual class as always
     needing construction/destruction so its vptr is stamped even with no user ctor/dtor.
     (this stage)

STAGE 1 (landed, PARSE only): the grammar accepts `virtual` before a method or the `~()`
destructor (parse.h is_virtual) and `virtual T m(...) = delete;` as a pure method (is_pure,
a bodyless kFunctionDecl). `virtual _()` (a virtual ctor) and `virtual` on a non-method
member are rejected at parse. Downstream still treats a virtual method as an ORDINARY
method (static dispatch) — for a single class with no override that is the same result, so
the class below compiles and runs. Pure methods and dispatch land in later stages.

STAGE 2 (landed, LAYOUT only): a ROOT virtual class (>=1 `virtual` member, no base) gets a
hidden `_$vptr` field (parse.h hasVptr) as its unnamed first field — a pointer at offset 0.
It flows through the class layout like `_$base`: sizeof grows by 8, field access still
resolves by name (so a user field just shifts one slot), and construction fills the vptr
with null (never a constructor argument; flatFieldWidth / classifyClassInit exclude it).
A DERIVED virtual class reuses the base's vptr (no `_$vptr` of its own). NOTHING reads the
vptr yet — the vtable global, the ctor stamp, and dispatch all land in stage 3.

STAGE 3 (landed): each virtual class emits a vtable global (`@<Class>__$vtable`, a
`[N x ptr]` of slot implementations) and its ctor stamps it into offset 0; flat
construction runs base ctors first, so the most-derived vtable ends up at offset 0. A
virtual method call (`self.m()`, `obj.m()`, `ptr^.m()`) loads the vptr, indexes the
method's slot, and calls indirect — so an override wins at runtime, even through a base
pointer, and an inherited method resolves to the base slot valid in every derived vtable.
The vtable layout: base slots first (stable index), overrides reuse the slot, new virtuals
append; OVERLOADED virtuals each take their own slot (overload resolution picks the slot at
compile time, the vptr picks the impl at run time). A qualified call is a STATIC bypass
(parse.h bypass_virtual, set by resolve for both statement and expression forms). ALL four
spellings bypass: `Base:m()` / `Self:m()` (base + own-class qualifier) and `Base:self.m()`
/ `Self:self.m()` (the same with an explicit `self.`). `selfOrBaseDepth` unifies them —
0 base hops for the own class, d for a transitive base — and `X:self.method()` (a method
call whose receiver is `X:self`) reframes the receiver + marks bypass. The method may be
one the qualified class INHERITS (is_method walks classAndBaseFrames), so `X:m()` bypasses
to the nearest inherited impl and agrees with `X:self.m()`. Only an unqualified `self.m()`
dispatches.

STAGE 4 (landed): the destructor occupies vtable SLOT 0 (fixed across the hierarchy). Its
impl is a per-class COMPLETE destructor `@<Class>__$vdtor` — the dtor body then the
reverse-order field + base-subobject chain. `delete base_ptr` on a virtual pointer loads
the object's vptr, reads slot 0, and calls it — so the most-derived destructor runs and
chains up, even through a base pointer. Scope exit of a KNOWN static type stays a direct
call (the dynamic type IS the static type). A mid-chain class with no explicit dtor is just
skipped in the chain (its `__$vdtor` runs no body, only the recursion). As teardown walks
toward the root, each class's dtor RE-STAMPS its own vtable at offset 0 first (the mirror of
construction's per-class stamp), so a virtual call from within a destructor dispatches to
the class BEING destroyed — never to a more-derived override whose object part is already
torn down (matching construction, where a virtual call in a base ctor reaches the base).
Still deferred: a
virtual class that declares NEITHER ctor nor dtor is not yet auto-given one — its vptr goes
unstamped on DIRECT instantiation (fine when only used as a base of a ctor-bearing class);
the empty ctor+dtor synthesis lands in stage 5.

STAGE 5 (landed): the virtual-class rules are enforced (validateVirtualClass in resolve) —
the base of a virtual class must be virtual; an explicit dtor must be virtual; an override
must be declared `virtual` and match the inherited return type; a virtual method may not
shadow a non-virtual one (nor vice versa). A PURE virtual (`= delete`) is bodyless (exempt
from "declared but never defined"), stays a valid dispatch target (its vtable slot is
`null`), and makes its class ABSTRACT — instantiating one is an error. That check lives at
the SINGLE construction funnel (classifyClassInit), so it fires uniformly for every
instantiation form — a local, a `new`, a temporary, an array/tuple element — while a base
subobject (a concrete derived completes its pure slots) and a by-value field (diagnosed at
the class definition) are exempt via a `subobject` flag. It constructs fine as the base
subobject of a concrete derived class. And the
stage-4 vptr-stamp gap is closed: a virtual class ALWAYS needs construction + destruction,
so its vptr is stamped at construction (emitConstructHooks) even with no user ctor/dtor —
no ctor/dtor node synthesis needed. A re-open of a virtual class is validated too: it may
override/implement an existing (inherited or original) virtual slot but may NOT introduce a
brand-new virtual method — all virtual methods must be in the original declaration.
*/

/* STAGE 1 — a virtual class parses, compiles, and runs. */
Vc(int x_) {
    _() {}
    virtual ~() { __println("Vc:dtor"); }
    virtual int get() { return x_; }
    virtual void show() { __println("Vc:show x=" + x_); }
}

/* STAGE 2 — a NON-virtual twin with the same field, to show the vptr's storage cost. */
Pc(int x_) {
    _() {}
    ~() {}
    int get() { return x_; }
}

/* STAGE 3 — an inheritance hierarchy exercising dynamic dispatch. */
Shape(int id_) {
    _() {}
    virtual ~() {}
    virtual void name() { __println("Shape"); }
    virtual int area() { return 0; }
    virtual void describe() { self.name(); }        /* self-call DISPATCHES */
    /* overloaded virtual methods — each a distinct vtable slot */
    virtual int scale(int k) { return k; }
    virtual int scale(int k, int j) { return k + j; }
}

Shape : Square(int side_) {
    _() {}
    virtual ~() {}
    virtual void name() { __println("Square"); }    /* override */
    virtual int area() { return side_ * side_; }     /* override */
    virtual int scale(int k) { return side_ * k; }   /* override one overload only */
    virtual void viabase() { Shape:name(); }         /* STATIC bypass -> Shape */
}

/* STAGE 4 — virtual destructors that print, to observe dispatch + chaining order. */
Animal(int a_) {
    _() {}
    virtual ~() { __println("Animal:dtor"); }
    virtual void speak() { __println("...(animal)"); }
}
Animal : Dog(int d_) {
    _() {}
    virtual ~() { __println("Dog:dtor"); }
    virtual void speak() { __println("woof"); }
}

/* A mid-chain class with NO explicit destructor: destruction chains right past it. */
Root3(int r_) {
    _() {}
    virtual ~() { __println("Root3:dtor"); }
    virtual int id() { return r_; }
}
Root3 : Mid3(int m_) {
    virtual int id() { return m_; }
}
Mid3 : Leaf3(int l_) {
    _() {}
    virtual ~() { __println("Leaf3:dtor"); }
    virtual int id() { return l_; }
}

/* STAGE 5 — a PURE virtual method makes Widget abstract; a concrete class implements it. */
Widget(int id_) {
    _() {}
    virtual ~() {}
    virtual void render() = delete;        /* pure -> Widget is abstract */
    virtual int cost() { return id_; }
}
Widget : Button(int price_) {
    _() {}
    virtual ~() {}
    virtual void render() { __println("Button:render"); }   /* implements the pure slot */
    virtual int cost() { return price_; }                    /* override */
}
/* A concrete class with NO explicit ctor/dtor — its vptr is stamped at construction. */
Widget : Label(int w_) {
    virtual void render() { __println("Label:render"); }
}

/* A re-open may IMPLEMENT an inherited virtual slot (here, an inherited pure) — that is
   an override, not a new virtual method, so it is allowed. */
Gadget(int g_) {
    _() {}
    virtual ~() {}
    virtual int tick() = delete;       /* pure */
}
Gadget : Clock(int t_) {
    _() {}
    virtual ~() {}
}
Clock() { virtual int tick() { return t_; } }   /* implement Gadget's pure via a re-open */

/* COVERAGE — a virtual method with a DEFAULT parameter. The default binds from the
   STATIC receiver type (the C++ rule); the dispatch is still dynamic. */
Def(int b_) {
    _() {}
    virtual ~() {}
    virtual int f(int k = 5) { return k; }
}
Def : Ddef(int d_) {
    _() {}
    virtual ~() {}
    virtual int f(int k = 9) { return k * 2; }   /* override; own default 9 (unused via a Def^) */
}

/* COVERAGE — a SYNTHESIZED complete-destructor (no user ctor/dtor on the derived) must
   still run field + base destruction. */
Noisy(int n_) { _() {} ~() { __println("Noisy:dtor"); } }
SynBase(int x_) {
    _() {}
    virtual ~() { __println("SynBase:dtor"); }
    virtual void m() {}
}
SynBase : SynDer(int y_, Noisy field_) {
    virtual void m() {}                          /* no ctor/dtor -> both synthesized */
}

/* COVERAGE — every dispatch-BYPASS spelling (own-class + base, bare + `self.`). A base
   method observed through a derived object distinguishes static bypass from dispatch. */
Bp(int bx_) {
    _() {}
    virtual ~() {}
    virtual void who() { __println("Bp:who"); }
    virtual void frombase() {
        Bp:who();          /* own-class qualifier -> static Bp:who */
        Bp:self.who();     /* own-class self.     -> static Bp:who */
        self.who();        /* dispatch            -> most-derived */
    }
}
Bp : Dp(int dy_) {
    _() {}
    virtual ~() {}
    virtual void who() { __println("Dp:who"); }
    virtual void fromderived() {
        Bp:who();          /* base qualifier      -> static Bp:who */
        Bp:self.who();     /* base self.          -> static Bp:who */
        Dp:who();          /* own-class qualifier -> static Dp:who */
        Dp:self.who();     /* own-class self.     -> static Dp:who */
        self.who();        /* dispatch            -> Dp:who */
    }
}
/* Gp does NOT override who() — it INHERITS Dp::who. A qualifier that names a class which
   only inherits the method must still bypass (to the nearest inherited impl), and both the
   `X:method()` and `X:self.method()` spellings must agree. */
Dp : Gp(int gz_) {
    _() {}
    virtual ~() {}
    virtual void frominherited() {
        Gp:who();          /* own class inherits who -> static Dp:who (nearest) */
        Gp:self.who();     /* same via self.         -> static Dp:who */
        Bp:who();          /* 2-level base qualifier -> static Bp:who */
    }
}

/* COVERAGE — a virtual call during CONSTRUCTION dispatches to the class being built, and
   during DESTRUCTION to the class being destroyed: NEVER to a more-derived override whose
   object part is not yet constructed / already torn down. */
Life(int x_) {
    _() { self.stage(); }              /* Life's ctor -> Life:stage (Grown not built yet) */
    virtual ~() { self.stage(); }      /* Life's dtor -> Life:stage (Grown already torn down) */
    virtual void stage() { __println("Life:stage"); }
}
Life : Grown(int y_) {
    _() {}
    virtual ~() {}
    virtual void stage() { __println("Grown:stage"); }
}

/* COVERAGE — non-int returns dispatched through the vtable: float64, int64, and a class
   BY VALUE (sret through the indirect call). */
Pair(int a_, int b_) { _() {} ~() {} int sum() { return a_ + b_; } }
Ret(int x_) {
    _() {}
    virtual ~() {}
    virtual float64 fp() { return 1.5; }
    virtual int64 big() { return 111; }
    virtual Pair make() { return Pair(1, 2); }
}
Ret : Ret2(int y_) {
    _() {}
    virtual ~() {}
    virtual float64 fp() { return 2.5; }
    virtual int64 big() { return 999; }
    virtual Pair make() { return Pair(10, 20); }
}

/* COVERAGE — an EMPTY virtual class (no user fields): the vptr alone (sizeof 8). */
Empty() {
    _() {}
    virtual ~() {}
    virtual int k() { return 42; }
}

int32 main() {
    {
        Vc v(7);
        __println("get = " + v.get());     // 7
        v.show();                          // Vc:show x=7

        /* STAGE 2: the vptr occupies real storage at offset 0 — a virtual class carries a
           pointer the same-field non-virtual class does not. Field access is unchanged
           (x_ still reads 7); only the layout grew. */
        int64 vsz = sizeof(Vc);            // vptr(8) + x_(4) + pad(4) = 16
        int64 psz = sizeof(Pc);            // x_(4) = 4
        __println("sizeof Vc = " + vsz);
        __println("sizeof Pc = " + psz);
    }                                      // Vc:dtor at scope exit

    /* STAGE 3 — dynamic dispatch. */
    {
        Shape sh(1);
        Square sq(1, 5);                   // flat construction: [id_=1 (base), side_=5]
        sh.name();                         // Shape (own)
        sq.name();                         // Square (override)
        sq.describe();                     // inherited describe -> self.name() -> Square
        __println("sq.area = " + sq.area());   // 25 (Square::area)

        Shape^ sp = ^sq;                   // a base pointer to a derived object
        sp^.name();                        // Square (dispatch through base ptr)
        __println("sp^.area = " + sp^.area()); // 25

        /* overloaded virtual dispatch: the slot is picked by signature, then dispatched. */
        __println("scale(3) = " + sp^.scale(3));      // Square::scale(int)  -> 5*3 = 15
        __println("scale(3,4) = " + sp^.scale(3, 4)); // inherited Shape::scale(int,int) -> 7

        sq.viabase();                      // STATIC bypass -> Shape
    }

    /* STAGE 4 — the virtual destructor dispatches on delete through a base pointer. */
    {
        Animal^ ap = new Dog(1, 2);        // a Dog behind an Animal pointer
        ap^.speak();                       // woof (dispatch)
        delete ap;                         // Dog:dtor then Animal:dtor (dispatch + chain)
    }
    {
        Leaf3 lf(1, 2, 3);                 // flat: [r_=1, m_=2, l_=3]
        __println("id = " + lf.id());      // 3 (Leaf3::id)
    }                                      // Leaf3:dtor then Root3:dtor (Mid3 has none)

    /* STAGE 5 — pure/abstract + a synthesized (no-ctor/dtor) concrete class. */
    {
        Button b(1, 50);
        b.render();                        // Button:render (implements the pure)
        __println("cost = " + b.cost());   // 50

        Widget^ wp = ^b;
        wp^.render();                      // dispatch through an abstract-base pointer

        Label lb(1, 8);                    // no ctor/dtor: vptr stamped at construction
        Widget^ wl = ^lb;
        wl^.render();                      // Label:render (dispatch)

        Clock ck(1, 5);                    // Clock's pure `tick` was implemented in a re-open
        __println("tick = " + ck.tick());  // 5 (dispatch to the re-opened impl)
    }

    /* COVERAGE — default parameters on a virtual method (static default, dynamic dispatch). */
    {
        Def^ dp = new Ddef(1, 2);
        __println("f() = " + dp^.f());     // default 5 (from the Def^ static type) -> Ddef -> 10
        __println("f(3) = " + dp^.f(3));   // Ddef::f(3) -> 6
        delete dp;
    }

    /* COVERAGE — a synthesized complete-dtor destructs field then base. */
    {
        SynDer sd(1, 2, 3);                // flat: [x_=1 (base), y_=2, field_=Noisy(3)]
    }                                      // Noisy:dtor then SynBase:dtor

    /* COVERAGE — multi-level dispatch + delete through a base pointer of a no-dtor mid. */
    {
        Root3^ rp = new Mid3(1, 2);        // Mid3 overrides id(), has NO dtor
        __println("mid id = " + rp^.id()); // 2 (dispatch to Mid3::id through a Root3^)
        delete rp;                         // Root3:dtor (Mid3 has no dtor -> chains up)

        Root3^ rl = new Leaf3(1, 2, 3);
        __println("leaf id = " + rl^.id());// 3 (dispatch to Leaf3::id through a Root3^)
        delete rl;                         // Leaf3:dtor then Root3:dtor (Mid3 skipped)
    }

    /* COVERAGE — dispatch-bypass spellings (own-class + base, bare + self.). */
    {
        Bp^ bp = new Dp(1, 2);
        bp^.frombase();                    // Bp:who, Bp:who, Dp:who
        Dp dd(1, 2);
        dd.fromderived();                  // Bp:who, Bp:who, Dp:who, Dp:who, Dp:who
        delete bp;

        Gp gp(1, 2, 3);                    // inherits who; bypass to the inherited impl
        gp.frominherited();                // Dp:who, Dp:who, Bp:who
    }

    /* COVERAGE — dispatch during construction / destruction. */
    {
        Grown g(1, 2);                     // ctor -> Life:stage (dispatch to class under construction)
        g.stage();                         // Grown:stage (fully built -> normal dispatch)
    }                                      // dtor -> Life:stage (dispatch to class under destruction)

    /* COVERAGE — non-int + class-by-value returns through the vtable. */
    {
        Ret^ rp = new Ret2(1, 2);
        __println("fp = " + rp^.fp());     // 2.5
        __println("big = " + rp^.big());   // 999
        Pair pr = rp^.make();              // sret through the indirect call -> Ret2's Pair(10,20)
        __println("sum = " + pr.sum());    // 30
        delete rp;
    }

    /* COVERAGE — copying a virtual object preserves the vptr. */
    {
        Dp a(1, 2);
        Dp b = a;                          // copy
        Bp^ cp = ^b;
        cp^.who();                         // Dp:who (dispatch works on the copy)
    }

    /* COVERAGE — an empty virtual class: vptr only. */
    {
        __println("empty sz = " + sizeof(Empty));   // 8
        Empty^ ep = new Empty();
        __println("empty k = " + ep^.k());           // 42
        delete ep;
    }
    return 0;
}

/* STAGE 1 negatives (parse). */

/* a constructor cannot be virtual. */
//-EXPECT-ERROR: A constructor cannot be virtual
//Nc(int x_) { virtual _() {} ~() {} }

/* `virtual` decorates only a method or the destructor. */
//-EXPECT-ERROR: 'virtual' may modify only a method or the destructor
//Nv(int x_) { _() {} virtual ~() {} virtual const int k = 1; }

/* STAGE 5 negatives (semantic checks). */

/* the base class of a virtual class must itself be virtual. */
//-EXPECT-ERROR: base class of a virtual class must itself be virtual
//NbP(int x_) { _() {} ~() {} }
//NbP : NbV(int y_) { _() {} virtual ~() {} virtual void m() {} }

/* an explicitly declared destructor of a virtual class must be virtual. */
//-EXPECT-ERROR: destructor of a virtual class must be virtual
//NdV(int x_) { _() {} ~() {} virtual void m() {} }

/* overriding a virtual method requires the `virtual` keyword. */
//-EXPECT-ERROR: must be declared 'virtual'
//NoB(int x_) { _() {} virtual ~() {} virtual void m() {} }
//NoB : NoD(int y_) { _() {} virtual ~() {} void m() {} }

/* a virtual override's return type must match the inherited method. */
//-EXPECT-ERROR: return type of override
//NrB(int x_) { _() {} virtual ~() {} virtual void m() {} }
//NrB : NrD(int y_) { _() {} virtual ~() {} virtual int m() { return 0; } }

/* a non-virtual method cannot be shadowed by a virtual one. */
//-EXPECT-ERROR: cannot shadow a non-virtual one
//NsB(int x_) { _() {} virtual ~() {} void m() {} }
//NsB : NsD(int y_) { _() {} virtual ~() {} virtual void m() {} }

/* an abstract class (with an un-implemented pure method) cannot be instantiated. */
//-EXPECT-ERROR: Cannot instantiate the abstract class
//NaB(int x_) { _() {} virtual ~() {} virtual void d() = delete; }
//int32 use_abstract() { NaB a(1); return 0; }

/* a re-open may not add a NEW virtual method — all virtual methods must be in the
   original declaration (a re-open may only override/implement an existing slot). */
//-EXPECT-ERROR: may not add the new virtual method
//NroB(int x_) { _() {} virtual ~() {} virtual void m() {} }
//NroB() { virtual void extra() {} }

/* a pure method ('= delete') is meaningless without dispatch — it must be virtual. */
//-EXPECT-ERROR: pure method ('= delete') must be virtual
//Npm(int x_) { _() {} virtual ~() {} void m() = delete; }

/* an abstract class cannot be embedded by value as a field (only by reference). */
//-EXPECT-ERROR: cannot embed the abstract class
//NfA(int x_) { _() {} virtual ~() {} virtual void draw() = delete; }
//NfBox(int b_, NfA a_) { _() {} ~() {} }

/* the abstract-instantiation check lives at the single construction funnel, so it also
   fires for an ARRAY of an abstract class (every element is a complete object). */
//-EXPECT-ERROR: Cannot instantiate the abstract class
//NarA(int x_) { _() {} virtual ~() {} virtual void draw() = delete; }
//int32 use_arr() { NarA a[2] = (1, 2); return 0; }

/* overloaded virtual methods whose signatures both match a call are ambiguous. */
//-EXPECT-ERROR: Ambiguous call
//Namb(int x_) { _() {} virtual ~() {}
//  virtual int f(int a) { return 1; }
//  virtual int f(int a, int b = 0) { return 2; } }
//int32 use_amb() { Namb n(1); n.f(3); return 0; }

/* multiple inheritance is not supported — a virtual class has exactly one base. */
//-EXPECT-ERROR: Expected function name
//Nmi_a(int a_) { _() {} virtual ~() {} virtual void f() {} }
//Nmi_b(int b_) { _() {} virtual ~() {} virtual void g() {} }
//Nmi_a, Nmi_b : Nmi_c(int c_) { _() {} virtual ~() {} }

/* a COVARIANT override return (a more-derived return type) is not allowed — an override's
   return type must match the inherited method exactly. */
//-EXPECT-ERROR: return type of override
//NcvB(int x_) { _() {} virtual ~() {} virtual NcvB^ me() { return ^self; } }
//NcvB : NcvD(int y_) { _() {} virtual ~() {} virtual NcvD^ me() { return ^self; } }
