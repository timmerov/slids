/*
test import and linked files.
this is the consumer file.

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

this file SYNTHESIZES NOTHING. every class it touches is declared in library.slh, so
this TU is not the sibling and emits only `declare`s — the complete ctor/dtor and the
default copy/move/swap it calls are all library.sl's. that is the whole point of the
test: `Animal dog(1,2)` constructs and destructs an object whose ctor body this file
has never seen.

what each line is actually holding down:
  hello_world()             a plain declared function across the seam (the first slice).
  Integer / Color:kRed      an alias + enum from the header. the declaration IS the
                            definition for those, so they cost no linkage at all.
  Animal dog(1,2)           a class across the seam, incl. the implicit dtor call at
                            the end of main. dog.print() links to @Animal__print — and
                            that symbol is why "is this overloaded?" must count DISTINCT
                            SIGNATURES: print's declaration and its definition are two
                            entries, so counting entries made the definer emit
                            @Animal__print.8 while this file called @Animal__print.
  Space:Float / Space:...   namespace-scoped statics.
  Space:goodbye_world()     a namespace MEMBER: it must mangle by SCOPE PATH
                            (@Space__goodbye_world), never by entry id, or this file and
                            library.sl number their entries differently and never link.
  Space:Vegetable peas(3,4) the nested-class case, same seam.
  note() / Util:tag() /     PRIVATE symbols this file defines. library.sl defines its OWN
  Widget:hum()              note(), Util:tag(), and a local class Widget with hum(), same
                            names and different bodies. none is declared in a header, so all
                            are `internal` — each TU calls its own. If a source-defined
                            function or a local-class method were external (the bug), the two
                            @note / @Util__tag / @Widget__hum defines would COLLIDE at link
                            and this program would not build. (Widget also proves the CLASS
                            path: a `.sl`-local class's methods are internal, unlike a header
                            class's, which stay external so importers link to them.)
the golden's line ORDER is load-bearing: the two dtors run in reverse declaration order
at the close of main.

the NoCtor negatives below are the same rule as the sibling's, and that is the point —
the ban is about the CLASS being declared in a header, not about which source file this
is. neither file may add one.
*/

import library;

/* PRIVATE to this TU — same names as library.sl's, distinct bodies. A free function,
   a namespace member, and a local CLASS METHOD: all three mangle to the same symbol as
   library.sl's (`@note`, `@Util__tag`, `@Widget__hum`), and coexist only because none
   is header-declared, so each is `internal` and each TU calls its own. */
void note() {
    __println("consumer: note");
}

int priv_ = 5;   /* .sl-LOCAL (not in the header) — PRIVATE, same name as library's 6 */

Util {
    void tag() {
        __println("consumer: Util:tag");
    }
}

Widget() {
    void hum() {
        __println("consumer: Widget:hum");
    }
}

int32 main() {

    note();
    Util:tag();
    Widget cw; cw.hum();

    // OVERLOADED header function, both arities — the cross-TU overload the old
    // per-TU entry-id suffix could not link.
    int a1 = add(10);      __println("add1: " + a1);
    int a2 = add(10, 20);  __println("add2: " + a2);

    // Same-arity overload discriminated by TYPE (int vs int64), and a TUPLE in the
    // signature — both cross the seam and must mangle identically in each TU.
    int p1 = pick(5);           __println("pick.int: " + p1);
    int64 big = 100;
    int p2 = pick(big);         __println("pick.i64: " + p2);
    (int, int) pr = (7, 8);
    int ps = pairsum(^pr);      __println("pairsum: " + ps);

    // `^` and `[]` are DISTINCT overloads — they must not share a mangled symbol.
    int y = 5;
    int pv = probe(^y);         __println("probe.ref: " + pv);   // int^  -> P -> int*
    int ya[2]; ya[0] = 1;
    int[] yi = ya;
    int pi = probe(yi);         __println("probe.iter: " + pi);  // int[] -> R -> int&

    hello_world();

    Integer x = Color:kRed; x;
    Animal dog(1,2);
    dog.print();
    int s1 = dog.sum(10);      __println("sum1: " + s1);      // OVERLOADED method
    int s2 = dog.sum(10, 20);  __println("sum2: " + s2);

    Space:Float f = 3.14; f;
    x = Space:Result:kSuccess;
    Space:Vegetable peas(3,4);
    peas.print();

    Space:goodbye_world();

    // Bird is declared in library.slh but defined in bird.sl — NOT the sibling. from
    // here that is invisible: same import, same construction, same call.
    Bird tweety(5,6);
    tweety.chirp();

    __println("who   = " + Query:who_);
    __println("what  = " + ::what_);
    __println("where = " + where_);
    __println("when  = " + Query:when_);

    // MUTATION across the seam: read library's init, write here, let library read+bump,
    // read library's write back — proves both TUs share ONE storage cell.
    __println("shared0 = " + shared_);   // 10, library's init read here
    shared_ = 99;                        // written here
    bump_shared();                       // library reads 99, writes 100
    __println("shared1 = " + shared_);   // 100, library's write read here

    // a COMPOUND (array) global built by the definer, read here.
    __println("nums  = " + nums[0] + " " + nums[1] + " " + nums[2]);
    // a header global defined in the NON-SIBLING bird.sl.
    __println("from_bird = " + from_bird);
    // this TU's PRIVATE priv_ (5); library's own priv_ (6) never collides.
    __println("consumer priv: " + priv_);

    return 0;
}

/*
in a source file, we cannot add a ctor/dtor or copy, move, swap operator to a class
first declared in a header file. the rule is the same here as in the sibling: it is
about the class being declared in a header, not about which source file this is.
*/
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
