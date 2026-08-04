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

import string;

import library;

/* PRIVATE to this TU — same names as library.sl's, distinct bodies. A free function,
   a namespace member, and a local CLASS METHOD: all three mangle to the same symbol as
   library.sl's (`@note`, `@Util__tag`, `@Widget__hum`), and coexist only because none
   is header-declared, so each is `internal` and each TU calls its own. */
void note() {
    println(String + "consumer: note");
}

int priv_ = 5;   /* .sl-LOCAL (not in the header) — PRIVATE, same name as library's 6 */

Util {
    void tag() {
        println(String + "consumer: Util:tag");
    }
}

Widget() {
    void hum() {
        println(String + "consumer: Widget:hum");
    }
}

/* returns an OPAQUE class by value across the seam: the caller's result slot is sized at
   runtime (@String__$sizeof) and the return is built into it by NRVO — no copy. */
Rope make_string() {
    Rope s;
    return s;
}

/* ── COMPUTED LAYOUT: local classes EMBEDDING imported incomplete classes BY VALUE.
   Each is laid out by the convention — static fields at natural offsets, an opaque
   field at the fixed opaque alignment (16) with its size read from the completer's
   absolute @<C>__$size16 — so every offset below is a compile-time constant plus a
   link-time relocation (or, past one symbolic term, a runtime sum of them). */

/* ONE opaque field between statics: post_'s offset is a single S+A relocation. */
Sack(int pre_ = 2, Rope r_, int post_ = 5) { }

/* TWO different opaque classes: end_'s offset sums two exported sizes — the
   multi-term case, link-time constants added at run time. Flat is the POD leaf:
   its fields still arrive {55, 0} through the routed @Flat__$ctor. */
Duo(Rope r_, int mid_ = 3, Flat f_, int end_ = 9) { }

/* an ARRAY of an opaque class as a FIELD: rs_[i] strides by @Rope__$size16 at run
   time, and z_ sits past a 2·size16 term. */
Trio(int n_ = 1, Rope rs_[2], int z_ = 4) { }

/* a RUNTIME-LAYOUT class (derives from Rope) as the embedded leaf: w_ rides
   @Tagged__$size16 — a size the completer folded over the real derived struct. */
TCase(Tagged t_, int w_ = 8) { }

/* Mix BY VALUE: its hidden fields (a nested Cell, an array) are built by the
   routed @Mix__$ctor inside THIS class's synthesized constructor. */
MCase(Mix m_, int k_ = 6) { }

/* a COMPLETE local class for the static iterator-difference case: the
   difference's divisor is the class's layout size, not a scalar width. */
Brick(int a_ = 1, int b_ = 2) { }

int32 main() {

    note();
    Util:tag();
    Widget cw; cw.hum();

    // OVERLOADED header function, both arities — the cross-TU overload the old
    // per-TU entry-id suffix could not link.
    int a1 = add(10);      println(String + "add1: " + a1);
    int a2 = add(10, 20);  println(String + "add2: " + a2);

    // Same-arity overload discriminated by TYPE (int vs int64), and a TUPLE in the
    // signature — both cross the seam and must mangle identically in each TU.
    int p1 = pick(5);           println(String + "pick.int: " + p1);
    int64 big = 100;
    int p2 = pick(big);         println(String + "pick.i64: " + p2);
    (int, int) pr = (7, 8);
    int ps = pairsum(^pr);      println(String + "pairsum: " + ps);

    // `^` and `[]` are DISTINCT overloads — they must not share a mangled symbol.
    int y = 5;
    int pv = probe(^y);         println(String + "probe.ref: " + pv);   // int^  -> P -> int*
    int ya[2]; ya[0] = 1;
    int[] yi = ya;
    int pi = probe(yi);         println(String + "probe.iter: " + pi);  // int[] -> R -> int&

    hello_world();

    Integer x = Color:kRed; x;
    Animal dog(1,2);
    dog.print();
    int s1 = dog.sum(10);      println(String + "sum1: " + s1);      // OVERLOADED method
    int s2 = dog.sum(10, 20);  println(String + "sum2: " + s2);

    // USER OPERATORS on a header-declared class are EXTERNAL here (bodies link from
    // library.sl). Each dispatch must match a DECLARED-not-defined operator — the
    // cross-TU hole. op=(int) -> findClassOperator; op[] -> classHasOperatorArity +
    // findClassOperator; op+ -> classHasOperatorArity + stampClassBinary. Each printed
    // value pins the dispatch (the bodies offset from a plain field-init).
    Counter cn = 5;                 // default-construct, then cn.op=(5) -> n_ = 6
    println(String + "counter: " + cn.get());        // 6
    int ce = cn[0];                 // (cn.op[](0))^ -> n_ -> 6
    println(String + "counter idx: " + ce);          // 6
    Counter cs = cn + 3;            // cs.op+(cn, 3) -> n_ = 6 + 3 = 9
    println(String + "counter sum: " + cs.get());    // 9

    Space:Float f = 3.14; f;
    x = Space:Result:kSuccess;
    Space:Vegetable peas(3,4);
    peas.print();

    Space:goodbye_world();

    // Bird is declared in library.slh but defined in bird.sl — NOT the sibling. from
    // here that is invisible: same import, same construction, same call.
    Bird tweety(5,6);
    tweety.chirp();

    println(String + "who   = " + Query:who_);
    println(String + "what  = " + ::what_);
    println(String + "where = " + where_);
    println(String + "when  = " + Query:when_);

    // MUTATION across the seam: read library's init, write here, let library read+bump,
    // read library's write back — proves both TUs share ONE storage cell.
    println(String + "shared0 = " + shared_);   // 10, library's init read here
    shared_ = 99;                        // written here
    bump_shared();                       // library reads 99, writes 100
    println(String + "shared1 = " + shared_);   // 100, library's write read here

    // a COMPOUND (array) global built by the definer, read here.
    println(String + "nums  = " + nums[0] + " " + nums[1] + " " + nums[2]);
    // a header global defined in the NON-SIBLING bird.sl.
    println(String + "from_bird = " + from_bird);
    // this TU's PRIVATE priv_ (5); library's own priv_ (6) never collides.
    println(String + "consumer priv: " + priv_);

    Rope hello;
    hello.set("goodbye");
    println(String + hello.get() + " " + hello.tag());

    // A second incomplete class: default-construct it and read every FIELD KIND the
    // completer's @Mix__$ctor initialized for us (we can see none of them). Proves the
    // ctor default-fills exactly like a normal site: a=11 (default), b=0 (NO default → 0),
    // s=4 (a recursively-built Cell: p_=4 default + q_=0), r=000 (array field zeroed).
    Mix mx;
    println(String + "mix a=" + mx.a() + " b=" + mx.b() + " s=" + mx.s()
            + " r=" + mx.r(0) + mx.r(1) + mx.r(2));

    // A POD incomplete class (no hooks). The importer can't see its fields, so it must
    // still call @Flat__$ctor — the "trivial POD, construct nothing" shortcut would leave
    // a_ / b_ uninitialized here. Expect a=55 (default), b=0 (no default).
    Flat fl;
    println(String + "flat a=" + fl.a() + " b=" + fl.b());

    // Cross-TU TRANSFER of an opaque class: its copy / move / swap ops are SYNTHESIZED and
    // EXTERNAL (library.sl emits them), so each call here links to the sibling's symbol.
    // str_ stays null (the importer can't set tag_, and copying a buffer-owning Rope would
    // double-free on its shallow memberwise copy — a Rope-semantics matter, not an opaque
    // one), so these prove the external transfer symbols link and the runtime-sized instances
    // build, copy, and destroy without crashing. tag_ is the default 7 throughout.
    // A class DERIVING from an opaque base. Its own fields are declared in the header and
    // ARE reachable here — but they sit past a base whose size only the sibling knows, so
    // each offset is read from that TU's exported @Tagged__$offsets rather than folded.
    // The instance is sized by @Tagged__$sizeof and filled by @Tagged__$ctor: mark_=3 and
    // pad_='!' from their defaults, extra_ has NO default so the ctor must zero it.
    Tagged tg;
    println(String + "tagged mark=" + tg.mark_ + " pad=" + tg.pad_ + " extra=" + tg.extra_);

    // WRITE through those offsets, then read back through a METHOD the sibling compiled
    // against the real struct. The two halves have to land on the same bytes: if the table
    // and the struct ever disagreed, the write and the read would pass each other.
    tg.mark_ = 40;
    tg.extra_ = 7;
    println(String + "tagged wrote mark=" + tg.mark() + " extra=" + tg.extra_);
    tg.bump();
    println(String + "tagged bumped=" + tg.mark());

    // BASE methods called on a DERIVED receiver, and a base field read by a derived method
    // over there. The base kept slot 0, so the receiver needs no adjustment at all.
    tg.set("derived");
    println(String + "tagged base: " + tg.get() + " " + tg.tag() + " " + tg.basetag());

    // UPCAST across the seam: a Tagged passed where a Rope^ is wanted. Free for the same
    // reason — slot 0 — which is why the layout keeps the base FIRST and pays for the
    // derived fields instead.
    println(String + "tagged upcast tag: " + strtag(^tg));

    // A single `new` of a derived-from-opaque class: malloc'd at the runtime size, fields
    // reached through the table exactly as the stack instance's are.
    Tagged^ tp = new Tagged;
    tp^.mark_ = 99;
    println(String + "tagged new: " + tp^.mark());
    delete tp;

    // Cross-TU TRANSFER of a derived class — copy / move / swap / assign each route to the
    // sibling's synthesized @Tagged__$*, which transfers the opaque base sub-object too.
    // Left with a null base str_ (see the Rope note above), so only mark_ is read back.
    Tagged tc; tc.mark_ = 21; Tagged tc2 = tc;         println(String + "tagged copy: " + tc2.mark_);
    Tagged tm; tm.mark_ = 22; Tagged tm2 <-- tm;       println(String + "tagged move: " + tm2.mark_);
    Tagged ts1; Tagged ts2; ts2.mark_ = 23; ts1 <--> ts2;  println(String + "tagged swap: " + ts1.mark_);
    Tagged ta; ta.mark_ = 24; Tagged tb; tb = ta;      println(String + "tagged assign: " + tb.mark_);

    Rope cp; Rope cp2 = cp;        println(String + "copy tag: " + cp2.tag());
    Rope mv; Rope mv2 <-- mv;      println(String + "move tag: " + mv2.tag());
    Rope sw1; Rope sw2; sw1 <--> sw2;  println(String + "swap tag: " + sw1.tag());
    Rope asrc; Rope adst; adst = asrc; println(String + "assign tag: " + adst.tag());
    Rope rv = make_string();         println(String + "return tag: " + rv.tag());

    // ── COMPUTED LAYOUT (the by-value embedding rejection is repealed) ──
    // A local class with an opaque field: statics before AND after the opaque one,
    // read/written directly, plus the opaque field driven through its methods.
    Sack sk;
    sk.r_.set("packed");
    println(String + "sack: " + sk.pre_ + " " + sk.r_.get() + " " + sk.r_.tag()
            + " " + sk.post_);
    sk.post_ = 50;
    println(String + "sack post: " + sk.post_);

    // TWO opaque leaves (Rope + the POD Flat): the multi-term offset, and the POD
    // routed-fill check (f_ must arrive {55, 0} even though Flat has no hooks).
    Duo du;
    println(String + "duo: " + du.mid_ + " " + du.f_.a() + " " + du.f_.b()
            + " " + du.r_.tag() + " " + du.end_);

    // an ARRAY FIELD of an opaque class: per-element construction, runtime stride,
    // and a static field past the 2·size16 term.
    Trio tr;
    tr.rs_[0].set("zero");
    tr.rs_[1].set("one");
    println(String + "trio: " + tr.n_ + " " + tr.rs_[0].get() + " "
            + tr.rs_[1].get() + " " + tr.rs_[1].tag() + " " + tr.z_);

    // a LOCAL ARRAY of an opaque class (the old negative): runtime-sized alloca,
    // stride off @Rope__$size16, each element routed through @Rope__$ctor/$dtor.
    Rope ra[3];
    ra[1].set("mid");
    println(String + "rope arr: " + ra[0].tag() + " " + ra[1].get());

    // a TUPLE SLOT holding an opaque class (the old negative).
    (Rope, int) tp2;
    tp2[1] = 12;
    println(String + "tuple slot: " + tp2[0].tag() + " " + tp2[1]);

    // TRANSFERS of the embedding class: the synthesized memberwise ops route the
    // Rope member through the sibling's @Rope__$copy/move/swap (never a blit).
    Sack sc; sc.post_ = 60; Sack sc2 = sc;   println(String + "sack copy: " + sc2.post_ + " " + sc2.r_.tag());
    Sack sm; sm.post_ = 61; Sack sm2 <-- sm; println(String + "sack move: " + sm2.post_);
    Sack sx1; Sack sx2; sx2.post_ = 62; sx1 <--> sx2; println(String + "sack swap: " + sx1.post_);
    Sack sa; sa.post_ = 63; Sack sb; sb = sa; println(String + "sack assign: " + sb.post_);

    // a single `new` of an embedding class: malloc'd at the convention size.
    Sack^ sp2 = new Sack;
    sp2^.post_ = 70;
    println(String + "sack new: " + sp2^.post_ + " " + sp2^.r_.tag());
    delete sp2;

    // THE SEAM TEST: Crate is a HEADER class embedding Rope, so both TUs compute
    // its layout independently by the convention. A direct write here must be seen
    // by a method the sibling compiled, and vice versa.
    Crate ho;
    ho.tail_ = 33;
    println(String + "crate direct->method: " + ho.tail());
    ho.stamp(44);
    println(String + "crate method->direct: " + ho.tail_ + " id=" + ho.id_
            + " rope=" + ho.r_.tag());

    // a runtime-layout leaf (Tagged prints its hooks) and a deep-hidden leaf (Mix).
    TCase tc3;
    println(String + "tcase: " + tc3.t_.mark_ + " " + tc3.w_);
    MCase mc;
    println(String + "mcase: " + mc.m_.a() + " " + mc.m_.s() + " " + mc.k_);

    // ── ITERATOR ARITHMETIC over runtime-sized elements: `iter ± int` and
    // `iter - iter` stride by the SAME convention size the array layout uses
    // (@Rope__$size16 / the layout expression) — never the opaque
    // placeholder's single byte. The identities against the array-index path
    // pin the agreement; the difference pins the divisor.
    int it1 = 0;
    Rope[] rp0 = ra;              // the Rope[3] local above, decayed
    Rope[] rp1 = rp0 + 1;
    Rope[] rp2 = rp1 + 2 - 1;     // the minus path
    intptr rn = rp2 - rp0;
    if (rn == 2 && (<intptr> rp1) == (<intptr> ^ra[1])
        && (<intptr> rp2) == (<intptr> ^ra[2])) {
        it1 = 1;
    }
    println(String + "iter opaque: " + it1);

    // an EMBEDDING (computed-layout) element: the stride is the convention
    // expression rather than a single exported symbol.
    int it2 = 0;
    Sack sarr[2];
    Sack[] sp0 = sarr;
    Sack[] sp1 = sp0 + 1;
    intptr sn = sp1 - sp0;
    if (sn == 1 && (<intptr> sp1) == (<intptr> ^sarr[1])) {
        it2 = 1;
    }
    println(String + "iter computed: " + it2);

    // the OTHER iterator steps ride the same funnel: `iter[i]` indexing and
    // the `++`/`--` bump.
    int it4 = 0;
    Rope[] qp = rp0;
    ++qp;
    ++qp;
    --qp;
    if ((<intptr> qp) == (<intptr> ^ra[1])
        && (<intptr> ^rp0[2]) == (<intptr> ^ra[2])) {
        it4 = 1;
    }
    println(String + "iter bump: " + it4);

    // a COMPLETE class element: the static path — the difference's divisor is
    // the class's layout size (the scalar-only divisor used to assert here).
    int it3 = 0;
    Brick ba[3];
    Brick[] bp0 = ba;
    Brick[] bp2 = bp0 + 2;
    intptr bn = bp2 - bp0;
    if (bn == 2 && (<intptr> bp2) == (<intptr> ^ba[2])) {
        it3 = 1;
    }
    println(String + "iter static: " + it3);

    // sizeof over the seam: an opaque size is a link-time value, an embedding
    // class's a convention expression. Relations, not absolutes, so the golden
    // survives library-side field edits.
    int size_ok = 0;
    if (sizeof(Rope) > 0 && sizeof(Sack) >= sizeof(Rope)
        && sizeof(Duo) >= sizeof(Sack)) {
        size_ok = 1;
    }
    println(String + "sizeof ok: " + size_ok);

    return 0;
}

/*
in a source file, we cannot add a ctor/dtor or copy, move, swap operator to a class
first declared in a header file. the rule is the same here as in the sibling: it is
about the class being declared in a header, not about which source file this is.
*/
//-EXPECT-ERROR: cannot add a constructor
//NoCtor:_() { println(String + "compile error."); }

//-EXPECT-ERROR: cannot add a destructor
//NoCtor:~() { println(String + "compile error."); }

//-EXPECT-ERROR: cannot add a copy operator
//NoCtor:op=(NoCtor^ rhs) { println(String + "compile error."); }

//-EXPECT-ERROR: cannot add a move operator
//NoCtor:op<--(mutable NoCtor^ rhs) { println(String + "compile error."); }

//-EXPECT-ERROR: cannot add a swap operator
//NoCtor:op<-->(mutable NoCtor^ rhs) { println(String + "compile error."); }

/*
Rope is declared incomplete in the header (`Rope(...)`) and completed in library.sl,
so from HERE its layout is opaque: this TU knows the class exists and can name its methods,
but not its fields, size, or offsets. Holding one by pointer, a bare local, a `new`, AND —
since the COMPUTED-LAYOUT landing — embedding one by value (a class field, an array
element, a tuple slot: see the Sack/Duo/Trio/Crate tests in main) are all legal: the
container is laid out by the cross-TU convention off the completer's exported absolute
sizes. What REMAINS illegal is exactly what still needs a fact this TU cannot have:
  - reaching a hidden field / passing a construction initializer (no field to see);
  - any GLOBAL whose storage would need the layout (static storage can't be
    runtime-sized) — bare opaque, or an aggregate/computed-class embedding one;
  - `new C[n]` (the heap-array machinery runs on a static element type);
  - a BY-VALUE parameter (the standing rule for every class: non-primitives pass
    by pointer).
*/

//-EXPECT-ERROR: has no field 'str_'
//void neg_field() { Rope h; int x = h.str_; x; }

//-EXPECT-ERROR: initializer(s) were given
//void neg_init() { Rope s("x"); s; }

/* a GLOBAL is where a BARE opaque class is still illegal: its storage is static
   (sized at compile time), so the runtime-sizing that makes a bare opaque LOCAL work
   has no analogue. A global must be a pointer instead. */
//-EXPECT-ERROR: a global needs static storage
//global Rope gbad;

//-EXPECT-ERROR: embeds incomplete class 'Rope'
//global Rope garr[3];

/* a global of an EMBEDDING class is the same static-storage problem one level up. */
//-EXPECT-ERROR: embeds incomplete class 'Rope'
//global Sack gsack;

/* `new C[n]` stays out: the heap-array machinery (cookie, broadcast fill, delete's
   element loop) runs on a static element type. A single `new C` is fine. */
//-EXPECT-ERROR: array of incomplete class
//void neg_newarr() { Rope[] p = new Rope[3]; delete p; }

/* the heap twin for an EMBEDDING (computed-layout) class. */
//-EXPECT-ERROR: array of incomplete class
//void neg_newemb() { Sack[] p = new Sack[3]; delete p; }

/* BY-VALUE parameters are prohibited — the standing non-primitive rule; an opaque
   class gets no special dispensation. */
//-EXPECT-ERROR: A non-primitive parameter must be a pointer
//int neg_param(Rope r) { return 0; }

/*
Tagged DERIVES from Rope, so it inherits that unknown layout wholesale: this TU can name
its fields and read and write them, but it cannot PLACE them. Default construction is the
only form here — an initializer would have to be written at an offset past a base only the
completer can measure, and dropping it silently would be worse than refusing it. Embedding
a Tagged by value is LEGAL now (TCase in main — the leaf rides @Tagged__$size16); the
global and heap-array bans hold for the identical static-storage/static-stride reasons.
*/

//-EXPECT-ERROR: can only be default-constructed here
//void neg_derived_init() { Tagged t(1); t; }

//-EXPECT-ERROR: a global needs static storage
//global Tagged gtbad;

//-EXPECT-ERROR: array of incomplete class
//void neg_derived_newarr() { Tagged[] p = new Tagged[3]; delete p; }
