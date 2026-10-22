/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test imported templates defined in another source file.
this is the test source file.
*/

/*
claude says:

the first consumer: every template kind across the seam — class flavors
(construction, methods, hooks firing through external symbols), function
templates inferred + explicit, an alias template (type-level, no demand), a
template method via the out-of-line body, a cross-header argument
(Box<Bird>, carried by the .sli's provenance import) — plus the LOCAL-type
path: Box<Loc> and tpick<Loc^> clone the implicitly-loaded template
source's bodies and emit internal here, the only TU that can. the
tpick/tpock pair pins the private-helper asymmetry: tpock's aggregated
flavor works (its body and `tick` live in tmpl_lib.o), its local-type
instance errors — a body leaning on TU-private names is emittable only by
its own TU.

`Twin` is the other half of a pair with bird.sl: THE SAME SYMBOL FROM TWO
TUs. both files declare a private file-scope class of that name with a
different layout, and both instantiate Box<Twin> and tdiff<Twin^>. a
file-scope class takes no def_id suffix, so the two TUs' flavors mangle to
the IDENTICAL strings and both objects link into this binary. the
local-type carve-out's `define internal` is the whole reason it works.
promote such a flavor to an external symbol and one of the two numbers
below reads the other TU's layout — silently, no diagnostic, no link
error.
*/

import string;

import+ tmpl_lib;
import library;

/* a bodyless template declaration belongs in a header. */
//-EXPECT-ERROR: must have a body
//T bad<T>(T v);

/* THE INCOMPLETE HEADER TEMPLATE IS NOT OPAQUE. `Grow<T>(...)` declares no fields, but
   the completing re-open lives in the template SOURCE — which every importer loads and
   whose template content survives the strip — so the flavor's layout is known in every
   TU that can name it. A flavor therefore embeds BY VALUE like any ordinary class: two
   of them here, so the second field's offset genuinely depends on the first's real size,
   and a trailing primitive proves the layout continues past them. A PLAIN opaque class
   (library.slh's Rope) is the opposite and still cannot be embedded — its completing
   re-open is stripped from the loaded source, so only its sibling ever sees the fields
   (consumer.sl's negatives). */
/* THIS TU's private `Twin`: ONE field and a different `tv()` — deliberately not
   bird.sl's shape, so a cross-TU mix-up would show in the printed number. */
/* a hybrid BY VALUE in another class (the container takes a computed layout) and as a
   global — the two sites whose prefix nothing but the site itself could fill. */
Hyhold(Hybrid h_, int t_) {
    void show() { h_.print(); println(String + "hyh.t = " + t_); }
}
global Hybrid hyg(8);

Twin(int t_ = 3) {
    int tv() { return t_ + 1; }
}

Holder(
    Grow<int> a_,
    Grow<int> b_,
    int tag_ = 9
) {
    int sum() { return a_.total() + b_.total() + tag_; }
    intptr counts() { return a_.count() + b_.count(); }
}

int32 main() {

    /* two flavors of the class template; a repeated flavor is one body. */
    Vector<int> vi;
    vi.push(5);
    vi.push(6);
    println(String + "vi = " + vi.sum());
    Vector<int8> v8(1, 2);
    println(String + "v8 = " + v8.sum());
    Vector<int> vi2(10, 20);
    println(String + "vi2 = " + vi2.sum());

    /* hooks defined across the seam fire per object. */
    {
        Traced<int> tt(3);
        println(String + "mid");
    }

    /* the function template: inferred and explicit. */
    int fs = tsum(3, 4); println(String + "fs = " + fs);
    int8 f8 = tsum<int8>(1, 2); println(String + "f8 = " + f8);

    /* the ARITY-overloaded pair across the seam: the argument count selects;
       one demand spelling (`tpair<int>`) emits BOTH aggregated flavors. */
    int t1 = tpair(5); println(String + "t1 = " + t1);
    int t2 = tpair(5, 6); println(String + "t2 = " + t2);
    int t3 = tpair<int>(7); println(String + "t3 = " + t3);

    /* the alias template (type-level only — no demand recorded). */
    TRef<int> tr = ^fs; println(String + "tr = " + tr^);

    /* INSTANCE-QUALIFIED members of an aggregated flavor from a consumer:
       the header's const and member alias, and a flavor minted BY the
       qualifier alone (no other Vector<int64> use — the demand rides). */
    int y1 = Vector<int>:kTag; println(String + "y1 = " + y1);
    Vector<int>:Elem ye = 5; println(String + "y2 = " + ye);
    int64 y3 = Vector<int64>:kTag; println(String + "y3 = " + y3);

    /* nested ALIAS TEMPLATES of a header class template — type-level, no
       demand recorded; INSTANCE-QUALIFIED is the one route (bare-host is
       out of the syntax; `<>` is rejected on header templates), and the
       outer-T target binds from the flavor. */
    Vector<int>:Ptr<int> ya = ^fs; println(String + "ya = " + ya^);
    Vector<int>:Ptr<int8> yb = ^f8; println(String + "yb = " + yb^);
    Vector<int>:Duo<int8> yd = (40, 5);
    int yd0 = yd[0];
    int8 yd1 = yd[1];
    println(String + "yd = " + yd0 + " " + yd1);

    /* the header-declared INCOMPLETE template (`Grow<T>(...)`): the source's
       completing re-open supplied the fields, so the flavor has the FULL
       layout here — methods work and the size is the real one. (A class's
       sizeof is always the `__$sizeof()` helper in this compiler, opaque or
       not — the layout is LLVM's; what changed is that the helper is emitted
       HERE off a real struct instead of imported from the completer.) */
    Grow<int> gr;
    gr.add(5);
    gr.add(7);
    println(String + "y4 = " + gr.total() + " " + gr.count());
    intptr y5 = sizeof(Grow<int64>) - 2 * sizeof(int64);
    println(String + "y5 = " + y5);

    /* a flavor EMBEDDED BY VALUE, twice: the second field sits past the first at a
       constant offset this TU can compute, and the fields are independent. */
    Holder h;
    h.a_.add(3);
    h.a_.add(4);
    h.b_.add(10);
    println(String + "y6 = " + h.sum() + " " + h.counts());
    intptr y7 = sizeof(Holder) - 2 * sizeof(Grow<int>);
    println(String + "y7 = " + y7);

    /* the template METHOD across the seam, inferred and explicit. */
    Gauge g(7);
    int ms = g.scaled(5); println(String + "ms = " + ms);
    int mx = g.scaled<int>(8); println(String + "mx = " + mx);
    int mb = g.base(); println(String + "mb = " + mb);

    /* an argument type from a DIFFERENT header (library's Bird): the .sli
       carries `import library;` so tmpl_lib.sl's instantiation can spell it. */
    Box<Bird> bb;
    println(String + "bb = " + bb.has());

    /* NESTED template-instance ARGUMENTS across the seam: the demand
       spellings round-trip through the .sli and aggregate like any flavor —
       one level (`Box<Vector<int>>`), two (`Box<Box<int>>`), and a function
       demand carrying a pointer to a nested instance (`tdiff<Vector<int>^>`
       — the read-only pointer-demand carrier; a bare-T body cannot RETURN
       a pointer binding under const correctness, canon B). */
    Vector<int> nv(2, 3);
    Box<Vector<int>> nxb;
    println(String + "nx1 = " + nxb.has());
    nxb.p_ = ^nv;
    println(String + "nx2 = " + nxb.p_^.sum());
    Box<int> ni;
    Box<Box<int>> nbb;
    nbb.p_ = ^ni;
    println(String + "nx3 = " + nbb.has() + " " + nbb.p_^.has());
    Vector<int> nv2(4, 4);
    bool npk = tdiff(^nv, ^nv2);
    println(String + "nx4 = " + npk);

    /* ITERATOR ARITHMETIC in an AGGREGATED flavor whose T is OPAQUE both here
       AND in the emitting TU (tstep<Rope>/tgap<Rope> bodies land in
       tmpl_lib.o, where Rope has no layout): the stride must be the
       convention's — and must agree with THIS TU's array-index stride. */
    Rope tw[3];
    Rope[] tw0 = tw;
    Rope[] tw2 = tstep(tw0, 2);
    intptr twn = tgap(tw0, tw2);
    int twok = 0;
    if (twn == 2 && (<intptr> tw2) == (<intptr> ^tw[2])) { twok = 1; }
    println(String + "tstep = " + twok);

    /* a LOCAL class instantiates an imported template INLINE: this TU loaded
       the template source's bodies and emits the flavor internal — it is the
       only TU that can. The aggregated flavors of the same templates are
       untouched (pk/po define once, in tmpl_lib.o). */
    Loc(int l_ = 0) {
        int lv() { return l_ + 1; }
    }
    Box<Loc> bl;
    println(String + "bl = " + bl.has());
    Loc lc(4);
    bl.p_ = ^lc;
    println(String + "bl2 = " + bl.has());
    println(String + "bl3 = " + bl.p_^.lv());
    Loc lc2(9);
    bool pl = tdiff(^lc, ^lc2);
    println(String + "pl = " + pl);
    int pk = tpick(3, 4); println(String + "pk = " + pk);
    int po = tpock(3, 4); println(String + "po = " + po);
    int tb = tbias(5); println(String + "tb = " + tb);

    /* THE VISIBILITY RULE, the overload half. This TU imports `library` (which
       declares pick(int) -> 1 and pick(int64) -> 2) and `tmpl_lib`, whose
       SOURCE imports tmpl_lib2 — so tmpl_lib2.slh's pick(int8) -> 3 is in this
       TU's token stream, registered, and its symbol is in tmpl_lib2.o, which
       this binary links. Only the rule keeps it out of the candidate set. An
       int8 argument is the discriminator: with the leak the exact match wins
       and this prints 3; with the rule only the public pair is ranked and the
       int8 widens into pick(int). The leak's whole danger is that it is silent,
       so the pin has to be a NUMBER. */
    int8 z8 = 5;
    int zpick = pick(z8); println(String + "zpick = " + zpick);

    /* THE VISIBILITY RULE, the name half. `Wrap2` is tmpl_lib2.slh's class
       template, reached here only through tmpl_lib.sl's own import — so it is
       unnameable in this file even though tmpl_lib.sl:182 instantiates it
       legitimately and consumer.sl (which imports tmpl_lib2 itself) uses it. */
    //-EXPECT-ERROR: is private to the template source that imported it
    //Wrap2<int> zw(21);
    //println(String + "zw = " + zw.dub());

    /* a template body that uses the source's PRIVATE helper can only be
       emitted by the source's own TU — a local-type instance gets the full
       cross-TU chain: use site, local class, template, the body's reference,
       the private definition, and the header remedy. */
    //-EXPECT-ERROR: private to its source
    //Loc na(1);
    //Loc nb(2);
    //Loc^ pb = tpock(^na, ^nb);
    //println(String + "pb = " + pb^.lv());

    /* the identifier flavor of the same chain: the body READS a private
       const — the note says "references 'kBias'". */
    //-EXPECT-ERROR: references 'kBias'
    //Loc ra(1);
    //Loc^ rb = tbias(^ra);
    //println(String + "rb = " + rb^.lv());

    /* a BARE-host use of a header template's nested alias: a pattern
       qualifier needs its list (bare-host is out of the syntax since the
       per-flavor repeal — instance-qualified is the route). */
    //-EXPECT-ERROR: requires a type-argument list
    //Vector:Duo<int8> nad = (1, 2);
    //int na0 = nad[0];
    //println(String + "na0 = " + na0);

    /* the LISTLESS flavor is TU-local machinery — rejected on a template
       declared in a header (no cross-TU story for `<>`). */
    //-EXPECT-ERROR: not available for a template declared in a header
    //Vector<>:Ptr<int> np = nullptr;
    //np;

    /* THE CARVE-OUT: a local-type instance of an imported class's template
       method emits `define internal` HERE — the one sanctioned exception to
       the owner-linkage rule (the flavor is unspellable anywhere else).
       CLASS bindings (the convention's value-copy form) since canon B: a
       bare-T body cannot RETURN a pointer binding. */
    Loc ml(2);
    Loc ml2(6);
    Loc mq = g.tsel(ml, ml2);
    println(String + "mq = " + mq.lv());

    /* a namespace-member template's local-type instance needs NO carve-out —
       a namespace member follows the file re-home, like a free function. */
    Loc nn = Spc2:nid(ml);
    println(String + "nn = " + nn.lv());

    /* SOURCE-PRIVATE FIELDS: the header's field list is the public interface;
       the completion's `sec_` is NOT a slot here — `Part<int>` is opaque, so
       PDer is a computed-layout class over it. The initializer tuple maps over
       the VISIBLE fields flat — `pub_` (base, public) takes 1, `sec_` (private,
       mid-layout) takes its author default from @Part<int>__$ctor, `zed_`
       takes 2. */
    PDer pd = (1, 2);
    println(String + "pv = " + pd.pub_ + " " + pd.zed_ + " " + pd.tally()
        + " " + pd.zz());
    /* the construction spelling filters identically. */
    Part<int> pc(5);
    println(String + "pc = " + pc.tally());
    /* the library-side witness: its own TU maps BOTH fields with the same
       spelling — privacy is importer-relative, deliberately. */
    println(String + "ps = " + partpriv());
    /* an EMPTY slot supplies a default to a VISIBLE field only — the slot
       positions, like the values, map over the visible sequence. */
    PDer pe(,6);
    println(String + "pe = " + pe.pub_ + " " + pe.zed_);
    /* a WHOLE-class copy is a transfer, not a field list: the synthesized
       memberwise op= is library code and carries `sec_` along intact. */
    Part<int> pcp = pc;
    println(String + "pcp = " + pcp.tally());
    /* the INLINE-LOCAL flavor keeps the boundary: Sack<Loc>'s bodies clone
       from the source and compile HERE, and their `hold_` accesses pass by
       POSITION; the consumer's own spelling rejects (negative below). */
    Sack<Loc> sk;
    println(String + "sk = " + sk.got());
    sk.put(^ml);
    println(String + "sk2 = " + sk.got());

    /* THE SAME SYMBOL FROM TWO TUs: this TU's flavors of Box<Twin> and
       tdiff<Twin^> carry the same mangled names as bird.sl's, and both
       objects are in this binary. Each call must reach ITS OWN TU's layout:
       the first number is this TU's one-field Twin, the second is bird.sl's
       two-field one, computed inside bird.o. */
    Twin w1;
    Box<Twin> bw;
    int wr = bw.has();
    bw.p_ = ^w1;
    wr = wr * 10 + bw.has();
    Twin w2(8);
    int wd = 0;
    if (tdiff(^w1, ^w2)) { wd = 1000; }
    int wsum = bw.p_^.tv() + wr + wd;
    println(String + "twin = " + wsum);
    int bsum = bird_twin();
    println(String + "btwin = " + bsum);

    /* a field the completion supplies cannot be READ by an importer. */
    //-EXPECT-ERROR: is private to its template source
    //Part<int> pr1(3);
    //int prx = pr1.sec_;
    //println(String + "prx = " + prx);

    /* ...nor WRITTEN. */
    //-EXPECT-ERROR: is private to its template source
    //Part<int> pr2(3);
    //pr2.sec_ = 9;
    //println(String + "pr2 = " + pr2.tally());

    /* ...nor reached through a DERIVED instance (the base-walk splice). */
    //-EXPECT-ERROR: is private to its template source
    //PDer pr3;
    //int pr3x = pr3.sec_;
    //println(String + "pr3 = " + pr3x);

    /* private fields are not initializer slots: the VISIBLE arity is the cap
       (the flat width counts `pub_` alone). */
    //-EXPECT-ERROR: 1 field(s) but 2 initializer(s)
    //Part<int> pr4(1, 2);
    //println(String + "pr4 = " + pr4.tally());

    /* an all-private completion (`Grow`) leaves NO visible slots — the same
       rejection an opaque class's hidden fields produce (Grow<int> IS opaque
       here). */
    //-EXPECT-ERROR: has private fields that cannot be set using initializer list syntax
    //Grow<int> pr5 = (5);
    //println(String + "pr5 = " + pr5.total());

    /* the reach through a POINTER is the same access. */
    //-EXPECT-ERROR: is private to its template source
    //Part<int> pr6(3);
    //Part<int>^ pr6p = ^pr6;
    //int pr6x = pr6p^.sec_;
    //println(String + "pr6 = " + pr6x);

    /* the INLINE-LOCAL flavor's private field rejects like the aggregated
       flavor's — position decides, not where the instance was emitted. */
    //-EXPECT-ERROR: is private to its template source
    //Sack<Loc> sn;
    //sn.hold_ = nullptr;
    //println(String + "sn = " + sn.got());

    /* public and private fields. */
    /*
    this syntax means copy the initializer list to the fields
    - iteratively and recursively.
    simple copy only.
    assignment operators may not be used.
    fields are initialized before the object constructor is called.
    */
    Hybrid hyba(1); hyba.print();
    /*
    this syntax means default construct the object.
    then copy/assign the rhs tuple elements to the fields
    - iteratively and recursively.
    fields are default initialized.
    the object constructor is called.
    then the fields are overwritten.
    */
    Hybrid hybb = 2; hybb.print();

    /* the private tail is not a slot a consumer can count, so supplying past the
       anchored prefix is an arity error — in either spelling. */
    //-EXPECT-ERROR: has 1 field(s) but 2 initializer(s)
    //Hybrid hybc(3,4); hybc.print();

    //-EXPECT-ERROR: has 1 field(s) but 2 initializer(s)
    //Hybrid hybd = (5,6); hybd.print();

    /* THE SAME SPLIT AT EVERY OTHER CONSTRUCTION SITE, because the site is the only
       place that can place the prefix: a HEAP object, a stack ARRAY (no list — the
       prefix still initializes, it does not go undefined), an EMBEDDED one (the
       enclosing site fills `h_.a` through the computed layout, and the enclosing ctor's
       slot walk reaches @Hybrid__$ctor for the private tail), and a GLOBAL (the
       first-touch thunk). Each prints its own `a` with the one `b` only the completer
       ever writes. */
    Hybrid^ hyp = new Hybrid(7); hyp^.print(); delete hyp;
    Hybrid hyar[2]; hyar[0].print(); hyar[1].print();
    Hyhold hyh(3, 4); hyh.show();
    hyg.print();
    println(String + "hybpriv = " + hybpriv());   // 102: the completer set both

    return 0;
}
