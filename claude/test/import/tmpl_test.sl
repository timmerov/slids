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

import tmpl_lib;
import library;

/* a bodyless template declaration belongs in a header. */
//-EXPECT-ERROR: must have a body
//T bad<T>(T v);

int32 main() {

    /* two flavors of the class template; a repeated flavor is one body. */
    Vector<int> vi;
    vi.push(5);
    vi.push(6);
    __println("vi = " + vi.sum());
    Vector<int8> v8(1, 2);
    __println("v8 = " + v8.sum());
    Vector<int> vi2(10, 20);
    __println("vi2 = " + vi2.sum());

    /* hooks defined across the seam fire per object. */
    {
        Traced<int> tt(3);
        __println("mid");
    }

    /* the function template: inferred and explicit. */
    int fs = tsum(3, 4); __println("fs = " + fs);
    int8 f8 = tsum<int8>(1, 2); __println("f8 = " + f8);

    /* the alias template (type-level only — no demand recorded). */
    TRef<int> tr = ^fs; __println("tr = " + tr^);

    /* the template METHOD across the seam, inferred and explicit. */
    Gauge g(7);
    int ms = g.scaled(5); __println("ms = " + ms);
    int mx = g.scaled<int>(8); __println("mx = " + mx);
    int mb = g.base(); __println("mb = " + mb);

    /* an argument type from a DIFFERENT header (library's Bird): the .sli
       carries `import library;` so tmpl_lib.sl's instantiation can spell it. */
    Box<Bird> bb;
    __println("bb = " + bb.has());

    /* a LOCAL class instantiates an imported template INLINE: this TU loaded
       the template source's bodies and emits the flavor internal — it is the
       only TU that can. The aggregated flavors of the same templates are
       untouched (pk/po define once, in tmpl_lib.o). */
    Loc(int l_ = 0) {
        int lv() { return l_ + 1; }
    }
    Box<Loc> bl;
    __println("bl = " + bl.has());
    Loc lc(4);
    bl.p_ = ^lc;
    __println("bl2 = " + bl.has());
    __println("bl3 = " + bl.p_^.lv());
    Loc lc2(9);
    Loc^ pl = tpick(^lc, ^lc2);
    __println("pl = " + pl^.lv());
    int pk = tpick(3, 4); __println("pk = " + pk);
    int po = tpock(3, 4); __println("po = " + po);

    /* a template body that uses the source's PRIVATE helper can only be
       emitted by the source's own TU — a local-type instance cannot inline
       it, and the helper's name does not resolve here. */
    //-EXPECT-ERROR: Unknown function
    //Loc na(1);
    //Loc nb(2);
    //Loc^ pb = tpock(^na, ^nb);
    //__println("pb = " + pb^.lv());

    /* a local-type instance of an imported class's TEMPLATE METHOD is
       deferred (its body would need to emit against a declare-only class). */
    //-EXPECT-ERROR: not supported
    //Loc ml(2);
    //Loc^ mq = g.scaled(^ml);
    //__println("mq = " + mq^.lv());

    return 0;
}
