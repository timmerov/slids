/*
test imported templates defined in another source file.
this is the SECOND consumer: the once-per-flavor union across consumers,
transfers and lifecycle across the seam, and the remaining instantiation
forms.
*/

import string;

import tmpl_lib;
import library;

alias Integer2 = int;

/* a consumer may not define a header template... */
//-EXPECT-ERROR: defined by its module's source
//T tsum<T>(T a, T b) { return a; }

/* ...nor re-open one. */
//-EXPECT-ERROR: defined by its module's source
//Vector<T>() {
//    T extra();
//}

/* a LOCAL class deriving from an AGGREGATED instance base. */
VShape<int> : MyDer(int m_ = 0) {
    virtual int vid() { return 100 + m_; }
    /* the `Base:` bypass naming the AGGREGATED instance base — the canon
       triple; the static call binds the base's EXTERNAL vid across the seam. */
    int trio() { return VShape<int>:s_ + VShape<int>:self.s_ + VShape<int>:vid(); }
}

/* a local class for the inline-local virtual / user-op= pins. */
Lok(int k_ = 0) {
    int kv() { return k_ + 1; }
}

/* an aggregated flavor as a GLOBAL (registry-constructed, external hooks). */
global Vector<int> gv2;

int32 main() {

    /* overlap with tmpl_test's demands (the union dedups to one body set),
       plus a flavor only this consumer wants. */
    Vector<int> a(1, 2);
    println(String + "a = " + a.sum());
    Vector<int16> v16(3, 4);
    int16 s16 = v16.sum(); println(String + "s16 = " + s16);

    /* transfers across the seam: the external __$copy / __$move / __$swap. */
    Vector<int> b = a;
    b.push(10);
    println(String + "t1 = " + a.sum() + " " + b.sum());
    Vector<int> m(7, 8);
    b <-- m;
    println(String + "t2 = " + b.sum());
    Vector<int> s1(1, 1);
    s1 <--> b;
    println(String + "t3 = " + s1.sum() + " " + b.sum());

    /* the USER op= declared in the header IS the flavor's canonical copy. */
    Acc<int> ax(5);
    Acc<int> ay = ax;
    println(String + "u1 = " + ax.get() + " " + ay.get());

    /* destructor balance through external hooks: a copy... */
    {
        Traced<int> c1(1);
        Traced<int> c2 = c1;
        println(String + "copied " + c2.get());
    }
    /* ...an array... */
    {
        Traced<int8> ta[2];
        println(String + "array");
    }
    /* ...and a temp, dead at its semicolon. */
    int tg = Traced<int>(9).get(); println(String + "tg = " + tg);

    /* the virtual flavor: direct, and dispatched through the base pointer
       into the local derived class. */
    VShape<int> vs(7);
    int r1 = vs.vid(); println(String + "r1 = " + r1);
    MyDer md(3, 4);
    VShape<int>^ vp = ^md;
    int r2 = vp^.vid(); println(String + "r2 = " + r2);
    int r2b = md.trio(); println(String + "r2b = " + r2b);

    /* the instance-qualified const through an already-demanded flavor. */
    int y4 = Vector<int16>:kTag; println(String + "y4 = " + y4);

    /* the header's nested alias template from the SECOND consumer — the
       outer T bound by a different flavor here (type-level, no demand). */
    Vector<int16>:Duo<int> y5 = (6, 700);
    int16 y50 = y5[0];
    int y51 = y5[1];
    println(String + "y5 = " + y50 + " " + y51);

    /* the SECOND consumer's flavors of the header-incomplete template: the
       shared flavor dedups across the seam; a fresh one demands its own. */
    Grow<int> g2;
    g2.add(3);
    Grow<int64> g3;
    g3.add(400);
    println(String + "y6 = " + g2.total() + " " + g3.total());

    /* two type parameters — a comma in the demand spelling. */
    TPair<int, int8> tp(300, 5);
    int r3 = tp.kk(); println(String + "r3 = " + r3);
    int8 r4 = tp.vv(); println(String + "r4 = " + r4);

    /* a POINTER type argument in the demand (`tdiff<Bird^>` — the read-only
       carrier; canon B). */
    Bird b1(1, 2);
    Bird b2(3, 4);
    bool pb = tdiff(^b1, ^b2);
    println(String + "pb = " + pb);
    b2.chirp();

    /* NESTED arguments from the SECOND consumer: the shared flavor
       (`Box<Vector<int>>`) dedups across the union; the fresh one carries an
       inner COMMA (`Box<TPair<int, int8>>`) — the depth-counting splitter
       must not split at the nested list's comma. */
    Box<Vector<int>> nb2;
    nb2.p_ = ^a;
    println(String + "nz1 = " + nb2.p_^.sum());
    Box<TPair<int, int8>> nbt;
    nbt.p_ = ^tp;
    println(String + "nz2 = " + nbt.p_^.kk() + " " + nbt.p_^.vv());

    /* the namespace-member template, qualified. */
    int nq = Spc2:nsq(5); println(String + "nq = " + nq);

    /* an ALIAS argument canonicalizes to the same demand and flavor. */
    Vector<Integer2> va(10, 1);
    println(String + "va = " + va.sum());

    /* the sibling's own flavor use, and its use of ANOTHER library's
       template (the mixed-role TU). */
    int w2 = viaW2(); println(String + "w2 = " + w2);
    int ow = viaOwn(); println(String + "ow = " + ow);

    /* the remaining instantiation forms of an aggregated flavor: the global,
       new, an array, a tuple slot; sizeof folds from the header's layout. */
    gv2.push(5);
    println(String + "g1 = " + gv2.sum());
    Vector<int>^ np = new Vector<int>;
    np^.push(3);
    println(String + "n1 = " + np^.sum());
    Vector<int8> varr[2];
    varr[0].push(1);
    varr[1].push(2);
    int8 va2 = varr[0].sum() + varr[1].sum(); println(String + "va2 = " + va2);
    (Vector<int>, int) vt = (a, 5);
    int r5 = vt[0].sum() + vt[1]; println(String + "r5 = " + r5);
    intptr zd = sizeof(Vector<int8>) - 2 * sizeof(int8);
    println(String + "zd = " + zd);

    /* inline-local flavors of the VIRTUAL and USER-op= templates: the vtable
       and the canonical copy emit internal here, riding the class-linkage
       override. The nulling user op= proves the USER's copy ran. */
    Lok lk(4);
    VShape<Lok^> vl(^lk);
    Lok^ vr = vl.vid();
    println(String + "vv = " + vr^.kv());
    Ucp<Lok> uc;
    uc.u_ = ^lk;
    println(String + "u2 = " + uc.un());
    Ucp<Lok> ud = uc;
    println(String + "u3 = " + ud.un());

    /* a source-side member NOT declared in the header is not part of a
       consumer's interface. */
    //-EXPECT-ERROR: priv2
    //int q1 = a.priv2(); println(String + "q1 = " + q1);

    return 0;
}
