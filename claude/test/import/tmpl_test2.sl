/*
test imported templates defined in another source file.
this is the SECOND consumer: the once-per-flavor union across consumers,
transfers and lifecycle across the seam, and the remaining instantiation
forms.
*/

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
}

/* an aggregated flavor as a GLOBAL (registry-constructed, external hooks). */
global Vector<int> gv2;

int32 main() {

    /* overlap with tmpl_test's demands (the union dedups to one body set),
       plus a flavor only this consumer wants. */
    Vector<int> a(1, 2);
    __println("a = " + a.sum());
    Vector<int16> v16(3, 4);
    int16 s16 = v16.sum(); __println("s16 = " + s16);

    /* transfers across the seam: the external __$copy / __$move / __$swap. */
    Vector<int> b = a;
    b.push(10);
    __println("t1 = " + a.sum() + " " + b.sum());
    Vector<int> m(7, 8);
    b <-- m;
    __println("t2 = " + b.sum());
    Vector<int> s1(1, 1);
    s1 <--> b;
    __println("t3 = " + s1.sum() + " " + b.sum());

    /* the USER op= declared in the header IS the flavor's canonical copy. */
    Acc<int> ax(5);
    Acc<int> ay = ax;
    __println("u1 = " + ax.get() + " " + ay.get());

    /* destructor balance through external hooks: a copy... */
    {
        Traced<int> c1(1);
        Traced<int> c2 = c1;
        __println("copied " + c2.get());
    }
    /* ...an array... */
    {
        Traced<int8> ta[2];
        __println("array");
    }
    /* ...and a temp, dead at its semicolon. */
    int tg = Traced<int>(9).get(); __println("tg = " + tg);

    /* the virtual flavor: direct, and dispatched through the base pointer
       into the local derived class. */
    VShape<int> vs(7);
    int r1 = vs.vid(); __println("r1 = " + r1);
    MyDer md(3, 4);
    VShape<int>^ vp = ^md;
    int r2 = vp^.vid(); __println("r2 = " + r2);

    /* two type parameters — a comma in the demand spelling. */
    TPair<int, int8> tp(300, 5);
    int r3 = tp.kk(); __println("r3 = " + r3);
    int8 r4 = tp.vv(); __println("r4 = " + r4);

    /* a POINTER type argument in the demand (`tpick<Bird^>`). */
    Bird b1(1, 2);
    Bird b2(3, 4);
    Bird^ pb = tpick(^b1, ^b2);
    pb^.chirp();

    /* the namespace-member template, qualified. */
    int nq = Spc2:nsq(5); __println("nq = " + nq);

    /* an ALIAS argument canonicalizes to the same demand and flavor. */
    Vector<Integer2> va(10, 1);
    __println("va = " + va.sum());

    /* the sibling's own flavor use, and its use of ANOTHER library's
       template (the mixed-role TU). */
    int w2 = viaW2(); __println("w2 = " + w2);
    int ow = viaOwn(); __println("ow = " + ow);

    /* the remaining instantiation forms of an aggregated flavor: the global,
       new, an array, a tuple slot; sizeof folds from the header's layout. */
    gv2.push(5);
    __println("g1 = " + gv2.sum());
    Vector<int>^ np = new Vector<int>;
    np^.push(3);
    __println("n1 = " + np^.sum());
    Vector<int8> varr[2];
    varr[0].push(1);
    varr[1].push(2);
    int8 va2 = varr[0].sum() + varr[1].sum(); __println("va2 = " + va2);
    (Vector<int>, int) vt = (a, 5);
    int r5 = vt[0].sum() + vt[1]; __println("r5 = " + r5);
    intptr zd = sizeof(Vector<int8>) - 2 * sizeof(int8);
    __println("zd = " + zd);

    /* a source-side member NOT declared in the header is not part of a
       consumer's interface. */
    //-EXPECT-ERROR: priv2
    //int q1 = a.priv2(); __println("q1 = " + q1);

    return 0;
}
