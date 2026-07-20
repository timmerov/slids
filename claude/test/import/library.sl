/*
test import and linked files.
this is the library source file.

everything defined in this source file is private.
private members have no visibility outside this source file.

in a source file, we cannot add a ctor/dtor or copy, move, swap
operator to a class first declared in a header file.

things to test:
two source files defining different local functions (or methods) with the same name.
link error when to two source files define the same header function (or method) with
the same name.
*/

/*
claude says:

this is library.slh's SIBLING — same base name (the directories would be allowed to
differ). that is what makes this file, and no other, responsible for emitting the
SYNTHESIZED members of every class the header declares: the complete ctor/dtor and the
default copy/move/swap. consumer.sl only DECLARES those and links here.

the split is between synthesized and declared, not between this file and the rest:
  - SYNTHESIZED (nobody wrote it) -> only the sibling can emit it. no source's location
    could select an owner, so the rule has to be positional.
  - DECLARED (a method, an operator, a hook body) -> definable in ANY ONE .sl. a header
    declaring several classes is meant to be able to give each one its own source file.
so everything defined below is here because it is convenient, not because it is forced.
defining it zero times or twice is a LINK error — this compiler sees one TU and cannot
know what the others define. that is the same deal hello_world() already gets.

the external `Vegetable:~()` and the block re-open `Vegetable() { _() {...} }` are two
spellings of the same thing, on purpose: a ctor/dtor is a method with restrictions, so
if a syntax works for a method it has to work here.

KNOWN GAP (todo: HOOK BODY IN A NON-SIBLING TU): a header-declared hook whose body sits
in a non-sibling .sl mis-compiles today — this file would emit the complete `@C__$ctor`
calling a `@C__$ctor__impl` it never declares, and llc rejects it with no diagnostic
from slidsc. so the hook bodies below are in the sibling because that is what WORKS, not
because the model requires it.
*/

import library;

/* PRIVATE to this TU — same names as consumer.sl's, distinct bodies: a free function, a
   namespace member, and a local class Widget with a method. Each is called from a header
   function this TU defines, so a consumer that links here still runs library's own copy.
   All are `internal`; were a source-defined function or a local-class method external (the
   bug), @note / @Util__tag / @Widget__hum would clash with consumer.sl's and the link
   would fail. Widget also covers the CLASS half — a `.sl`-local class's methods are
   internal, whereas a header class's stay external so importers link to them. */
void note() {
    __println("library: note");
}

Util {
    void tag() {
        __println("library: Util:tag");
    }
}

Widget() {
    void hum() {
        __println("library: Widget:hum");
    }
}

int priv_ = 6;   /* .sl-LOCAL (not in the header) — PRIVATE, same name as consumer's */

void hello_world() {
    String hw(nullptr, 42);
    hw.set("Hello, World!");
    __println(hw.get() + " " + hw.tag());
    // DEFINER-side default construction (no initializer): this TU owns the layout, so it
    // fills the field DEFAULTS at the site (str_=null, tag_=7) and calls @String__$pctor
    // directly — the site-fill path, distinct from an importer's @String__$ctor default-fill.
    String dc;
    __println("dc tag: " + dc.tag());
    note();
    Widget lw; lw.hum();
    __println("library priv: " + priv_);   // library's own internal priv_
}

Animal() {
    _() {
        __println("Animal:ctor: " + a + " " + b);
    }
    ~() {
        __println("Animal:dtor: " + a + " " + b);
    }
    op=(Animal^ rhs) {
        a = rhs^.a;
        b = rhs^.b;
        __println("Animal:op=: " + a + " " + b);
    }
}

void Animal:print() {
    __println("Animal:print: " + a + " " + b);
}

/* Counter's USER operators — declared in the header, so DEFINING them here is legal (the
   sibling owns the bodies). Each result is offset from a plain field-init so the importer's
   printed value proves the operator DISPATCHED rather than a construction winning. */
Counter:op=(int a) {
    n_ = a + 1;
}
int Counter:get() {
    return n_;
}
int^ Counter:op[](int i) {
    i;
    return ^n_;
}
Counter:op+(Counter^ a, int b) {
    n_ = a^.n_ + b;
}

/* the definitions of the header's overloaded function + method. */
int add(int a) {
    return a;
}
int add(int a, int b) {
    return a + b;
}

int pick(int x) {
    x;
    return 1;
}
int pick(int64 x) {
    x;
    return 2;
}

int pairsum((int, int)^ p) {
    int a; int b;
    (a, b) = p^;
    return a + b;
}

int probe(int^ r) {
    r;
    return 1;
}
int probe(int[] it) {
    it;
    return 2;
}

int Animal:sum(int x) {
    return a + x;
}
int Animal:sum(int x, int y) {
    return a + x + y;
}

Space {
    void goodbye_world() {
        __println("Goodbye, World!");
        Util:tag();
    }

    Vegetable() {
        _() {
            __println("Vegetable:ctor: " + a + " " + b);
        }
    }
    Vegetable:~() {
        __println("Vegetable:dtor: " + a + " " + b);
    }
}

void Space:Vegetable:print() {
    __println("Vegetable:print: " + a + " " + b);
}

global Query(who_ = 1) { }
global what_ = 2;
int where_ = 3;
global Query(when_ = 4) {
    _() {
        __println("Query:ctor");
    }
    ~() {
        __println("Query:dtor");
    }
}

/* the definitions of the header's cross-TU data globals. */
int shared_ = 10;
void bump_shared() {
    shared_ = shared_ + 1;   // reads the other TU's write, writes back
}
global int nums[3] = (7, 8, 9);
/* from_bird is DEFINED in bird.sl, not here — this TU only declares it (via the header). */

/* complete the incomplete class */
String(char[] str_ = nullptr, int tag_ = 7) {
    _() {}
    ~() { delete str_; }

    void set(char[] s) {
        intptr len = 0;
        while (s[len]) { ++len; }
        str_ = new char[len+1];
        while (len >= 0) { str_[len] = s[len]; --len; }
    }
    char[] get() { return str_; }
    int tag() { return tag_; }
}

/* helper for Mix's by-value class field: p_ has a default, q_ does NOT — so a default Cell
   must come out {4, 0}, proving the recursive fill zeros a nested no-default field too. */
Cell(int p_ = 4, int q_) { }

/* complete Mix (see the header). Its @Mix__$ctor — synthesized as a placement-new — must
   default-construct all four hidden fields for an importer: a_ from its default, b_ to zero,
   s_ recursively (a default Cell), and every element of r_ to zero. */
Mix(int a_ = 11, int b_, Cell s_, int r_[3]) {
    _() { }
    ~() { }
    int a() { return a_; }
    int b() { return b_; }
    int s() { return s_.p_ + s_.q_; }
    int r(int i) { return r_[i]; }
}

/* complete Flat — a POD (no hooks). a_ has a default, b_ does not, so an importer's default
   construction via @Flat__$ctor must come out {55, 0}. */
Flat(int a_ = 55, int b_) {
    int a() { return a_; }
    int b() { return b_; }
}

/*
in a source file, we cannot add a ctor/dtor or copy, move, swap operator to a class
first declared in a header file. these five are IMPLICITLY invoked, so every importing
TU emits calls to them from the header alone — adding one here would make this TU
disagree with every other about what constructing or copying a NoCtor does, silently.
the ban is on ADDING: Animal's hooks and op= ARE declared in the header, so defining
them (above) is legal. this file is the sibling, and it is still not allowed.
*/
/* the header declares `int mismatch_ = 2;` — DEFINING it with a different value is an
   error. (In the normal build mismatch_ is only declared, never defined, so no conflict.) */
//-EXPECT-ERROR: differs from its declaration
//int mismatch_ = 5;

//-EXPECT-ERROR: cannot add a constructor
//NoCtor:_() { __println("compile error."); }

//-EXPECT-ERROR: cannot add a destructor
//NoCtor:~() { __println("compile error."); }

//-EXPECT-ERROR: cannot add a copy operator
//NoCtor:op=(NoCtor^ rhs) { __println("compile error."); }

//-EXPECT-ERROR: cannot add a move operator
//NoCtor:op<--(mutable NoCtor^ rhs) { __println("compile error."); }

//-EXPECT-ERROR: cannot add a swap operator
//NoCtor:op<-->(mutable NoCtor^ rhs) { __println("compile error."); }

/* the block re-open form is the same violation. */
//-EXPECT-ERROR: cannot add a constructor
//NoCtor() {
//    _() { __println("compile error."); }
//    ~() { __println("compile error."); }
//}
