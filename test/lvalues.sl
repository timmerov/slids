/*
test lvalue parsing and assignment dispatch.
shapes: variable | field | index | deref | parens
operators: = | <- | <-> | += -= *= /= (compound deferred to feature C)
*/

Counter(int v_ = 0) {
    void incr() { v_ = v_ + 1; }
}

Pair(int x_ = 0, int y_ = 0) {}

void mutate_param(int p) {
    p = 99;
    __println("mutate_param: p=" + p);
}

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

        slid_tup = (Pair(1, 2), Pair(3, 4));
        slid_tup[1].x_ = 99;
        __println("tup-elem-field: slid_tup[1].x_=" + slid_tup[1].x_);
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

        int arrQ[4] = (1, 2, 3, 4);
        int[] q1 = ^arrQ[0];
        int[] q2 = ^arrQ[3];
        (q1++)^ <-> (q2--)^;
        __println("paren-swap: arrQ=" + arrQ[0] + "," + arrQ[1] + "," + arrQ[2] + "," + arrQ[3]);
    }

    /* swap <-> */
    {
        __println("-- swap --");

        int a = 1; int b = 2;
        a <-> b;
        __println("var: a=" + a + " b=" + b);

        int arrS[4] = (1, 2, 3, 4);
        arrS[0] <-> arrS[3];
        __println("inline-arr: arrS=" + arrS[0] + "," + arrS[1] + "," + arrS[2] + "," + arrS[3]);

        /* aspirational (C'): slid swap via default op<-> */
        // Pair pa(1, 2);
        // Pair pb(3, 4);
        // pa <-> pb;
        // __println("slid: pa=(" + pa.x_ + "," + pa.y_ + ") pb=(" + pb.x_ + "," + pb.y_ + ")");
    }

    /* move <- */
    {
        __println("-- move --");

        int[] s = new int[3];
        s[0] = 1; s[1] = 2; s[2] = 3;
        int[] d <- s;
        if (s == nullptr) {
            __println("s is null after move");
        }
        __println("d[0]=" + d[0]);
        delete d;
    }

    /* compound (feature C) */
    {
        __println("-- compound (C) --");

        // int x = 10;
        // x += 5;
        // x -= 2;
        // x *= 2;
        // x /= 4;
        // __println("x=" + x);
    }

    return 0;
}
