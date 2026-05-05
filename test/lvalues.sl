/*
test lvalue parsing and assignment dispatch.
shapes: variable | field | index | deref | parens
operators: = | <- | <-> | += -= *= /=
*/

Counter(int v_ = 0) {
    void incr() { v_ = v_ + 1; }
}

Pair(int x_ = 0, int y_ = 0) {}

/* Buffer: exercises slid op<op>= direct dispatch and the chain-build
   optimization (Buffer + a + b + ... emits one alloca + repeated op+=). */
Buffer(int count_ = 0, int last_int_ = 0, char[] last_str_ = nullptr) {
    op+=(char[] s) {
        count_ = count_ + 1;
        last_str_ = s;
    }
    op+=(int v) {
        count_ = count_ + 1;
        last_int_ = v;
    }
}

void mutate_param(int p) {
    p = 99;
    __println("mutate_param: p=" + p);
}

/* Types for the chained / generic lvalue tests at the bottom of main(). */
S(int x_ = 0) {
    op=(int v) { x_ = v; }
    op=(S^ rhs) { x_ = rhs^.x_; }
    op<-(mutable S^ rhs) {
        x_ = rhs^.x_;
        rhs^.x_ = 0;
    }
}
Inner(int x_ = 0) {}
Outer(Inner i_ = Inner()) {}
Holder(S inner_ = S()) {}
Box(int^ p_ = nullptr) {}

int show(S^ s) { return s^.x_; }

int32 main() {

    /* variables: local, param, self, inline array */
    {
        __println("-- variables --");

        int x = 1;
        x = 10;
        __println("local: x=" + x);

        mutate_param(5);

        Counter c(0);
        c.incr();
        c.incr();
        __println("self-field: c.v_=" + c.v_);

        int arrV[4] = (1, 2, 3, 4);
        arrV[2] = 30;
        __println("inline-arr: arrV[2]=" + arrV[2]);
    }

    /* field access: obj, ptr, tuple */
    {
        __println("-- field access --");

        Pair p(1, 2);
        p.x_ = 10;
        __println("obj-field: p.x_=" + p.x_);

        Pair^ pp = ^p;
        pp^.y_ = 20;
        __println("deref-field: p.y_=" + p.y_);
    }

    /* index: array, iterator, tuple */
    {
        __println("-- index --");

        int arrI[4] = (10, 20, 30, 40);
        arrI[1] = 200;
        __println("inline-arr: arrI[1]=" + arrI[1]);

        int[] it = ^arrI[0];
        it[2] = 333;
        __println("iter: arrI[2]=" + arrI[2]);

        tup = (1, 2, 3);
        tup[0] = 100;
        __println("tup: tup[0]=" + tup[0]);
    }

    /* pointer dereference: ptr-expr ^ */
    {
        __println("-- pointer deref --");

        int arrD[4] = (10, 20, 30, 40);
        int[] p = ^arrD[0];

        p^ = 11;
        __println("var^: arrD[0]=" + arrD[0]);

        p++^ = 22;
        __println("post-inc^: arrD[1]=" + arrD[1]);

        p--^ = 33;
        __println("post-dec^: arrD[1]=" + arrD[1]);

        (++p)^ = 44;
        __println("pre-inc^: arrD[1]=" + arrD[1]);
        (--p)^ = 55;
        __println("pre-dec^: arrD[0]=" + arrD[0]);
    }

    /* parentheses: ( lvalue ) is transparent */
    {
        __println("-- parens --");

        int xp = 1;
        (xp) = 7;
        __println("(xp)=: xp=" + xp);

        int arrP[4] = (1, 2, 3, 4);
        int[] p1 = ^arrP[0];
        int[] p2 = ^arrP[3];
        (p1++)^ = 100;
        (p2--)^ = 200;
        __println("paren-inc-deref: arrP=" + arrP[0] + "," + arrP[1] + "," + arrP[2] + "," + arrP[3]);
    }

    /* swap <-> — inline-array element. var swap and slid swap are
       in swap.sl; iterator-with-paren swap is the post-inc-deref form
       covered there too. */
    {
        __println("-- swap --");

        int arrS[4] = (1, 2, 3, 4);
        arrS[0] <-> arrS[3];
        __println("inline-arr: arrS=" + arrS[0] + "," + arrS[1] + "," + arrS[2] + "," + arrS[3]);
    }

    /* compound on each lvalue shape */
    {
        __println("-- compound primitive --");

        int x = 10;
        x += 5;  __println("x+=5: " + x);
        x -= 2;  __println("x-=2: " + x);
        x *= 2;  __println("x*=2: " + x);
        x /= 4;  __println("x/=4: " + x);
        x %= 3;  __println("x%=3: " + x);

        int y = 100;
        int^ py = ^y;
        py^ += 50;
        __println("py^+=50: y=" + y);

        Pair p(1, 2);
        p.x_ += 10;
        p.y_ *= 5;
        __println("p.x_+=10 p.y_*=5: (" + p.x_ + "," + p.y_ + ")");

        int arrC[4] = (10, 20, 30, 40);
        arrC[1] += 100;
        arrC[3] -= 5;
        __println("arrC: " + arrC[0] + "," + arrC[1] + "," + arrC[2] + "," + arrC[3]);
    }

    /* compound on slid LHS — direct op<op>= dispatch */
    {
        __println("-- compound slid op<op>= --");

        Buffer bd;
        bd += "hello";
        bd += 42;
        bd += "world";
        __println("bd: count=" + bd.count_ + " last_int=" + bd.last_int_);
    }

    /* chain optimization preservation: Buffer + ... produces one alloca with
       repeated op+= dispatch — not a chain of op+ temps. */
    {
        __println("-- chain optimization --");

        bc = Buffer + "x" + 1 + "y" + 2 + "z" + 3;
        __println("chain bc: count=" + bc.count_ + " last_int=" + bc.last_int_);
    }

    /* ---------- chained / generic lvalue shapes ---------- */

    /* DeleteStmt nullify on field chain and inline-array slot */
    {
        __println("-- delete + nullify chains --");

        Box bx(new int(7));
        delete bx.p_;
        bool nb = (bx.p_ == nullptr);
        __println("box.p_ nulled=" + nb);

        int^ pa[2];
        pa[0] = new int(1);
        pa[1] = new int(2);
        delete pa[0];
        bool np = (pa[0] == nullptr);
        __println("pa[0] nulled=" + np);
        delete pa[1];
    }

    /* FieldAssign — chained slid field write+read */
    {
        __println("-- chained field assign --");

        Outer o;
        o.i_.x_ = 99;
        __println("o.i_.x_=" + o.i_.x_);
    }

    /* FieldAssign — slid array element field */
    {
        __println("-- slid array element field --");

        S sa[3];
        sa[1].x_ = 77;
        __println("sa[1].x_=" + sa[1].x_);
    }

    /* IndexAssign — slid array op= / op<- dispatch */
    {
        __println("-- slid array op= / op<- --");

        S aa[2];
        S sc(77);
        aa[0] = sc;
        __println("aa[0].x_=" + aa[0].x_);

        S ab[2];
        S sm(88);
        ab[0] <- sm;
        __println("ab[0].x_=" + ab[0].x_);
        __println("sm.x_ after move=" + sm.x_);
    }

    /* FieldAssign — post-inc-deref + field */
    {
        __println("-- post-inc-deref + field --");

        S ar[3];
        S[] pp = ^ar[0];
        pp++^.x_ = 42;
        __println("ar[0].x_=" + ar[0].x_);
    }

    /* CompoundAssign on chained shapes */
    {
        __println("-- compound chains --");

        Outer oc;
        oc.i_.x_ = 5;
        oc.i_.x_ += 7;
        __println("oc.i_.x_=" + oc.i_.x_);

        S ac[3];
        ac[1].x_ = 10;
        ac[1].x_ += 5;
        __println("ac[1].x_=" + ac[1].x_);
    }

    /* AddrOf — chained field, post-inc-deref, post-inc-deref+field */
    {
        __println("-- addr-of chains --");

        Outer oa;
        oa.i_.x_ = 1;
        int^ ra = ^oa.i_.x_;
        ra^ = 99;
        __println("via ra: oa.i_.x_=" + oa.i_.x_);

        int ad[3] = (10, 20, 30);
        int[] pd = ^ad[0];
        int^ rb = ^(pd++^);
        rb^ = 99;
        __println("via rb: ad[0]=" + ad[0]);

        S sf[3];
        S[] pf = ^sf[0];
        int^ rc = ^pf++^.x_;
        rc^ = 42;
        __println("via rc: sf[0].x_=" + sf[0].x_);
    }

    /* ++/-- on FieldAccess and ArrayIndex */
    {
        __println("-- ++/-- on chains --");

        Counter cn(5);
        ++cn.v_;
        __println("++cn.v_: " + cn.v_);

        int ai[3] = (10, 20, 30);
        ai[1]++;
        __println("ai[1]++: " + ai[1]);
    }

    /* Auto-promote chained args to T^ params */
    {
        __println("-- auto-promote chains --");

        Holder h;
        h.inner_.x_ = 42;
        int g1 = show(h.inner_);
        __println("show(h.inner_)=" + g1);

        S sh[3];
        sh[1].x_ = 77;
        int g2 = show(sh[1]);
        __println("show(sh[1])=" + g2);
    }

    return 0;
}
