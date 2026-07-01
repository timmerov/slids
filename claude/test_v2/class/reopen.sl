/*
test re-opening classes.

classes can be declared in any scope.
they can be re-opened one or more times in the same scope.
the class declaration (with fields) must precede all re-opens.
from the author's perspective, it's as if everything was declared
within a single class definition.

    Class(int x) {
        /* alias, constant, enum, global, method, namespace, class */
    }
    Class() {
        /* more alias, constant, enum, global, method, namespace, class */
    }

the external form uses the class name as a qualifier.
it applies only to the current declaration.

    alias Class:Alias = int;
    const int Class:kConst = 7;
    enum int Class:Enum ( kZero );
    global int Class:g_counter = 0;
    int Class:method() { }
    Class:Namespace { }
    Class:Reopen() { }

these all conceptually desugar to:

    Class() {
        alias Alias = int;
        const int kConst = 7;
        enum int Enum ( kZero );
        global int g_counter = 0;
        int method() { }
        Namespace { }
        Reopen() { }
    }

the external form does not include hoisted classes.
the syntax is specifically excluded because it is ambiguous with
the syntax for derived classes.

    Class:NotAHoistedClass(int x) { }
    Class:DerivedClass(int x) { }

the external form may chain qualifiers.

    int Class1:Namespace1:Class2:Namespace2:Class3:method() { }

note:
global variables are not currently implemented.
*/

/*
claude says:

STAGE A (landed): BLOCK re-open. A same-name class with an EMPTY field list re-opens
the existing class — its members (const / enum / alias / method / nested class) merge
into the one class frame, visible bare or qualified across ALL openings. The primary
(field-bearing) definition must come first; a field-bearing re-open is "already
complete". No new layout — the kSlid is the primary's.

STAGE B (landed): the abbreviated `Class:member` form. A qualified const / alias
(`const int C:k = 7;`, `alias C:A = int;`) and an OUT-OF-LINE method
(`int C:method() { ... }`) define a member of C out of line — the method sees C's
fields / consts / methods bare, exactly as if written inside a re-open block. A
MULTI-SEGMENT path (`A:B:m`) targets a nested member, walking scope-in-scope through
classes AND namespaces (`Class1:Ns1:Class2:m`) and searching all openings at each
level (so a nested scope introduced in a re-open is reachable). The external form
also defines whole nested SCOPES: `Class:Namespace { }` adds a namespace member, and
`Class:Reopen() { }` (EMPTY parens) re-opens a hoisted class. A field-bearing head
`Class:Name(fields) { }` is NOT this form — it stays inheritance (a derived class),
so a hoisted class with fields is written in a block, not by qualified name. Still out
of scope: `global` vars, `...`.

STAGE E (landed): the external form is NOT file-scope-bound. Every external form
(method / namespace / hoisted-class re-open / const / alias / enum) applies in ANY scope
the class is DECLARED in — file, namespace body, class body, or function body / nested
block. relocateOutOfLineMembers runs per-scope (file + registerScopeNames + resolveStmtList
/ resolveFunctionBody) and MOVES the qualified node into its target — a LOCAL sibling
opening — so the target's ordinary registration handles it, no special-casing. A CLASS is
re-opened only in its OWN scope: a class merely VISIBLE from an enclosing scope is not a
local sibling, so re-opening it there (REFINE) is rejected per-segment ('X' is not a class
or namespace in scope). A NAMESPACE, by contrast, opens in ANY scope: a leaf
(const/alias/enum) whose first segment names an enclosing-scope namespace is registered
into that namespace's frame IN PLACE (registerQualifiedLeaf), left for constfold. A
qualified MUTABLE var is not a member; a bad qualifier errors per-segment.

STAGE F (landed): the external ENUM form (`enum int Class:E ( kZero );`) — a NAMED enum
defined out of line. Its members are reached qualified (`Class:E:member`, or `E:member`
from inside the class — a named enum's members are never bare). Enum joins const / alias /
method / namespace / re-open as a full external member, in every scope. (`global` stays
out — not implemented.)
*/

/* STAGE A — a primary + block re-opens: later openings add members that see the
   primary's field, const, and method bare (and vice-versa). */
Rc(int a_) {
    const int kOne = 1;
    int base_m() { return a_ + kOne; }        // primary field + primary const
}

Rc() {                                        // re-open: add a const + a method
    const int kTwo = 2;
    int more_m() { return base_m() + kTwo; }  // calls the PRIMARY method across openings
}

Rc() {                                        // second re-open (chained)
    int last_m() { return more_m() + kOne; }  // sees a prior RE-OPEN's method + primary const
}

Rc() { }                                      // empty re-open — a legal no-op

/* STAGE B — an OUT-OF-LINE method: `int Rc:oli()` defines a member of Rc, seeing its
   field / const / (re-open) method bare, as if written in a re-open block. */
int Rc:oli() { return base_m() + kOne; }      // primary method + primary const

/* an enum + alias added by a re-open, reached bare and qualified. */
En(int e_) {
    int base() { return e_; }
}
En() {
    enum ( kTag = 9 );
    alias Num = int;
    int tagged() { Num n = e_ + kTag; return n; }   // re-open enum + alias, bare
}

/* STAGE C — a re-open of a BASE class is visible on a DERIVED instance (the base
   re-open method rides the _$base chain). */
Bc(int a_) {
    int base_m() { return a_; }
}
Bc() {                                        // re-open the base: add a method
    int extra() { return a_ + 100; }
}
Bc : Dc(int b_) {                             // derived from the re-opened base
    int deriv() { return a_ + b_; }
}

/* STAGE C — a NEW nested class introduced by a re-open, then re-opened itself. */
Host(int h_) {
    int hm() { return h_; }
}
Host() {                                      // re-open Host: introduce a nested class
    Nested(int n_) {
        int nm() { return n_; }
    }
}
Host() {                                      // re-open Host again: re-open the nested class
    Nested() {
        int nm2() { return nm() + 1; }        // re-open method sees the primary nested method
    }
}

/* STAGE B — a MULTI-SEGMENT out-of-line method: `Host:Nested:nm3` targets a nested
   class that was itself introduced in a RE-OPEN of Host (searched across openings). */
int Host:Nested:nm3() { return nm() + 100; }

/* STAGE B — `Class:Namespace { }`: a NAMESPACE member added to a class by the
   external form. Its const/function are reached via the full qualified path. */
Ns(int x_) {
    int xm() { return x_; }
}
Ns:Space {
    const int kN = 42;
    int f() { return kN + 1; }         // a namespace free function (no receiver)
}

/* STAGE B — `Class:Reopen() { }`: an external RE-OPEN of a hoisted class (empty
   parens). The added method sees the hoisted class's own field/method bare. */
Bag(int b_) {
    Item(int v_) {
        int im() { return v_; }
    }
}
Bag:Item() {                           // re-open the hoisted class Bag:Item
    int extra() { return im() + 100; }
}

/* STAGE B — a chained path through a NAMESPACE: `Ov:Md:Inr:cm` targets a class
   nested in a namespace nested in a class (the walk crosses class + namespace). */
Ov(int o_) {
    Md {
        Inr(int i_) {
            int im() { return i_; }
        }
    }
}
int Ov:Md:Inr:cm() { return im() + 10; }

/* STAGE B — external Class:Namespace MERGES with an in-block namespace of the same
   name, may be re-opened by the external form repeatedly, and carries the full member
   vocabulary (const / alias / enum / function seeing every merged member bare). */
Mg(int m_) {
    Space { const int kA = 1; }
}
Mg:Space { const int kB = 2; }                // external form re-opens the block namespace
Mg:Space {                                    // ...again, adding an alias + enum
    alias Num = int;
    enum ( kTag = 20 );
    int sum() { Num n = kA + kB + kTag; return n; }   // sees every merged member bare
}

/* STAGE B — external Class:Reopen() of a hoisted class adds NON-method members (a
   const and a nested class) and reaches the HOST's namespace member bare, exactly as
   an in-block hoisted class does. */
Hc(int h_) {
    const int kHost = 1000;
    Item(int v_) { int im() { return v_; } }
}
Hc:Item() {
    const int kX = 50;
    Sub(int s_) { int sm() { return s_; } }
    int viaHost() { return kHost + im(); }    // host const bare + own method
}

/* STAGE B — CHAINED external SCOPE defs (not just methods): a namespace def and a
   class re-open reached through a class:namespace:class path. */
Cx(int c_) {
    Md {
        Bx(int b_) { int bm() { return b_; } }
    }
}
Cx:Md:Bx:Leaf { const int kL = 9; }           // chained NAMESPACE def into Cx:Md:Bx
Cx:Md:Bx() { int extra() { return 7; } }      // chained class RE-OPEN of Cx:Md:Bx

/* STAGE B — the full header chain shape: a method reached through
   Class1:Namespace1:Class2:Namespace2:Class3:method (3 classes / 2 namespaces). */
G1(int g_) {
    N1 {
        G2(int g2_) {
            N2 {
                G3(int g3_) { int g3m() { return g3_; } }
            }
        }
    }
}
int G1:N1:G2:N2:G3:deep() { return g3m() + 5; }

/* STAGE B — a namespace segment INTRODUCED IN A RE-OPEN of the enclosing class is
   reachable by the chained walk (openings are searched across namespaces too). */
Ro(int r_) { }
Ro() {
    Sp {
        In(int i_) { int im() { return i_; } }
    }
}
int Ro:Sp:In:rm() { return im() + 30; }       // Sp introduced in a re-open of Ro

/* STAGE E — a STANDALONE external const + external alias at FILE scope (alias newly
   landed), plus an external method that reads BOTH bare. */
Efc(int a_) {
    int base() { return a_; }
}
const int Efc:kAdd = 100;                      // external const, file scope
alias Efc:Num = int;                           // external alias, file scope
int Efc:combine() { Num n = a_ + kAdd; return n; }   // ext method sees ext const+alias

/* STAGE E — external forms INSIDE A NAMESPACE body (the class is declared there): an
   external const, alias, and method on a namespace-local class. */
Ens {
    Ec(int c_) { int cm() { return c_; } }
    const int Ec:kQ = 5;
    alias Ec:Num = int;
    int Ec:plus() { Num n = cm() + kQ; return n; }
}

/* STAGE F — the external ENUM form at FILE scope: a named enum defined out of line, its
   members reached qualified; a method reads a member via the enum name. */
Efe(int a_) {
    int base() { return a_; }
}
enum int Efe:Col ( kR, kG, kB );               // external enum, file scope (kB = 2)
int Efe:pick() { return base() + Col:kB; }     // ext method reads ext enum member

/* STAGE F — all external members INSIDE A NAMESPACE body, including an enum. */
Enf {
    Nc(int c_) { int cm() { return c_; } }
    const int Nc:kQ = 5;
    alias Nc:Num = int;
    enum int Nc:E ( kZero, kOne, kTwo );
    int Nc:go() { Num n = cm() + kQ + E:kTwo; return n; }   // cm + 5 + 2
}

/* STAGE F — CLASS-BODY scope: external const / alias / enum / method written inside a
   class body, each targeting a SIBLING hoisted class (re-opened in the class scope). */
Outer(int o_) {
    Sib(int s_) { int sm() { return s_; } }
    const int Sib:kC = 4;
    alias Sib:Num = int;
    enum int Sib:E ( kA, kB );
    int Sib:go() { Num n = sm() + kC + E:kB; return n; }    // sm + 4 + 1
}

int32 main() {
    Rc r = (10);
    __println("base = " + r.base_m());        // 10 + 1 = 11
    __println("more = " + r.more_m());        // 11 + 2 = 13
    __println("last = " + r.last_m());        // 13 + 1 = 14
    __println("oli = " + r.oli());            // 11 + 1 = 12 (OUT-OF-LINE method)

    En en = (5);
    __println("tagged = " + en.tagged());     // 5 + 9 = 14
    __println("en.qual = " + En:kTag);        // 9 (re-open enum via qualifier)

    Dc d = (1, 2);                            // a_=1 (base), b_=2 (derived)
    __println("d.base = " + d.base_m());      // 1   (base method)
    __println("d.extra = " + d.extra());      // 101 (BASE RE-OPEN method on a derived)
    __println("d.deriv = " + d.deriv());      // 3

    Host:Nested nst = (7);
    __println("nst.nm = " + nst.nm());        // 7 (primary nested method)
    __println("nst.nm2 = " + nst.nm2());      // 8 (nested RE-OPEN method calls primary nested)
    __println("nst.nm3 = " + nst.nm3());      // 107 (MULTI-SEGMENT out-of-line method)

    __println("ns.kN = " + Ns:Space:kN);      // 42  (Class:Namespace const)
    __println("ns.f = " + Ns:Space:f());      // 43  (Class:Namespace function)

    Bag:Item it = (5);
    __println("it.extra = " + it.extra());    // 105 (Class:Reopen() of a hoisted class)

    Ov:Md:Inr z = (3);
    __println("z.cm = " + z.cm());            // 13  (chained path through a namespace)

    __println("mg.sum = " + Mg:Space:sum());  // 23  (merged block + external namespace)
    __println("mg.kA = " + Mg:Space:kA);      // 1   (from the in-block opening)
    __println("mg.tag = " + Mg:Space:kTag);   // 20  (enum in an external opening)

    Hc:Item hi = (5);
    __println("hc.kX = " + Hc:Item:kX);       // 50  (const via external re-open)
    __println("hc.viaHost = " + hi.viaHost()); // 1005 (host const bare + own method)
    Hc:Item:Sub sub = (7);
    __println("hc.sub = " + sub.sm());        // 7   (nested class via external re-open)

    __println("cx.kL = " + Cx:Md:Bx:Leaf:kL); // 9   (chained namespace def)
    Cx:Md:Bx cb = (3);
    __println("cx.extra = " + cb.extra());     // 7   (chained class re-open)

    G1:N1:G2:N2:G3 g3 = (4);
    __println("deep = " + g3.deep());          // 9   (full C:N:C:N:C:method chain)

    Ro:Sp:In ri = (2);
    __println("ro.rm = " + ri.rm());           // 32  (namespace segment from a re-open)

    /* STAGE E — external const + alias + method at FILE scope. */
    Efc ef = (5);
    __println("efc.base = " + ef.base());       // 5
    __println("efc.combine = " + ef.combine()); // 105  (a_ + kAdd, via ext alias Num)
    __println("efc.kAdd = " + Efc:kAdd);        // 100  (external const, qualified)

    /* STAGE E — external forms inside a NAMESPACE body. */
    Ens:Ec ec = (3);
    __println("ec.plus = " + ec.plus());        // 8    (cm() + kQ)
    __println("ec.kQ = " + Ens:Ec:kQ);          // 5    (external const in target frame)

    /* STAGE E — external forms in a FUNCTION body on a body-local class: an external
       method / const / alias, an external hoisted-class re-open, and an external
       namespace def — all re-opening the body-local class in this same scope. */
    Elc(int v_) {
        Item(int i_) { int im() { return i_; } }
    }
    const int Elc:kL = 30;
    alias Elc:Num = int;
    int Elc:viaExt() { Num n = v_ + kL; return n; }   // ext method + ext const + alias
    Elc:Item() { int extra() { return im() + 1; } }    // ext hoisted-class re-open
    Elc:Sp { const int kB = 8; }                        // ext namespace def

    Elc el = (7);
    __println("el.via = " + el.viaExt());       // 37   (v_ + kL)
    Elc:Item eit = (9);
    __println("el.item = " + eit.extra());      // 10   (im() + 1, via ext re-open)
    __println("el.sp = " + Elc:Sp:kB);          // 8    (ext namespace const)

    /* STAGE F — external ENUM at file scope, namespace body, and class body. */
    Efe efe = (5);
    __println("efe.pick = " + efe.pick());      // 7    (base + Col:kB)
    __println("efe.col = " + Efe:Col:kB);       // 2    (ext enum member, qualified)
    Enf:Nc nc = (3);
    __println("nc.go = " + nc.go());            // 10   (cm + kQ + E:kTwo)
    __println("nc.e = " + Enf:Nc:E:kOne);       // 1    (ext enum in a namespace body)
    Outer:Sib os = (6);
    __println("os.go = " + os.go());            // 11   (sm + kC + E:kB, class-body forms)
    __println("os.kC = " + Outer:Sib:kC);       // 4
    __println("os.e = " + Outer:Sib:E:kB);      // 1

    /* STAGE F — external ENUM in a FUNCTION body on a body-local class. */
    Lce(int v_) { int lm() { return v_; } }
    enum int Lce:E ( kX, kY, kZ );
    int Lce:sum() { return lm() + E:kZ; }        // lm + 2
    Lce le = (5);
    __println("le.sum = " + le.sum());          // 7
    __println("le.e = " + Lce:E:kY);            // 1
    return 0;
}

/* STAGE D negatives. */

/* a re-open may not add fields — the layout is the primary's. */
//-EXPECT-ERROR: Duplicate definition of class 'Nf'; a re-open cannot add fields
//Nf(int a_) { }
//Nf(int b_) { }

/* re-declaring a member across openings is a duplicate. */
//-EXPECT-ERROR: Duplicate declaration of 'kDup'
//Rm(int a_) { const int kDup = 1; }
//Rm() { const int kDup = 2; }

/* a class STATIC does not leak unqualified outside the class. */
//-EXPECT-ERROR: 'kHid' needs a namespace qualifier
//Rk(int a_) { const int kHid = 5; }
//int32 leak() { return kHid; }

/* an out-of-line method's qualifier must name a class (or namespace) in scope. */
//-EXPECT-ERROR: 'Nope' is not a class or namespace in scope
//int Nope:m() { return 0; }

/* a multi-segment out-of-line path whose nested segment names no scope — the caret
   and message name the specific failing segment against its parent scope. */
//-EXPECT-ERROR: 'Onest' has no class or namespace member 'Gone'
//Onest(int x_) { Inr(int y_) { } }
//int Onest:Gone:m() { return 0; }

/* an external qualified form whose target names no class/namespace DECLARED in the
   current scope errors per-segment. (The external form now works in any scope the
   class is declared — STAGE E — so this is a bad-target error, not a scope
   restriction; NestQ has no member 'Inner'.) */
//-EXPECT-ERROR: 'Inner' is not a class or namespace in scope
//NestQ(int x_) { Inner:Deep { const int kD = 1; } }

/* REFINE reject — re-opening a CLASS from a scope where it is only VISIBLE (not
   declared) is out. An external CONST targeting a file-scope class from a function body
   is not a local sibling, so it errors per-segment (a namespace would be allowed; a
   class is same-scope only). */
//-EXPECT-ERROR: 'Rfc' is not a class or namespace in scope
//Rfc(int a_) { }
//int32 refuse_c() { const int Rfc:k = 1; return 0; }

/* REFINE reject — same for an external ENUM targeting a merely-visible class. */
//-EXPECT-ERROR: 'Rfe' is not a class or namespace in scope
//Rfe(int a_) { }
//int32 refuse_e() { enum int Rfe:E ( kZ ); return 0; }
