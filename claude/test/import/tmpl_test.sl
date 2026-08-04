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
*/

import string;

import tmpl_lib;
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

    return 0;
}
