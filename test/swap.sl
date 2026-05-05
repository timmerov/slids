/*
unit tests for swap operator.
*/

/* class with swap operator. */
Swap(int a_ = 0, int b_ = 0, int^ p_ = nullptr) {
    op<->(mutable Swap^ s) {
        __println("Swap[op<->]=op<->");
        a_ <-> s^.a_;
        p_ <-> s^.p_;
    }
}

/* class without swap operator. */
NoSwap(int a_ = 0, int b_ = 0, int^ p_ = nullptr) {
}

/* class with wrong swap operator. */
Wrong() {
    /* commented out compiler test:
    op<->(mutable NoSwap^ ns) {
    }
    */
}

int32 main() {
    /* swap ints */
    int x1 = 1;
    int y1 = 2;
    x1 <-> y1;
    __println("x1[2]=" + x1);
    __println("y1[1]=" + y1);

    /* swap pointers */
    int xp1 = 10;
    int yp1 = 20;
    int^ pa = ^xp1;
    int^ pb = ^yp1;
    pa <-> pb;
    bool pok_a = (pa == ^yp1);
    bool pok_b = (pb == ^xp1);
    __println("pok_a[1]=" + pok_a);
    __println("pok_b[1]=" + pok_b);
    __println("pa^[20]=" + pa^);
    __println("pb^[10]=" + pb^);

    /* swap tuples */
    int xt1 = 100;
    int yt1 = 200;
    (int, int^) t1 = (1, ^xt1);
    (int, int^) t2 = (2, ^yt1);
    t1 <-> t2;
    __println("t1[0][2]=" + t1[0]);
    __println("t2[0][1]=" + t2[0]);
    bool tok_a = (t1[1] == ^yt1);
    bool tok_b = (t2[1] == ^xt1);
    __println("tok_a[1]=" + tok_a);
    __println("tok_b[1]=" + tok_b);

    /* swap class with overload */
    int xs1 = 1000;
    int ys1 = 2000;
    Swap sw1(10, 11, ^xs1);
    Swap sw2(20, 21, ^ys1);
    sw1 <-> sw2;
    __println("sw1.a_[20]=" + sw1.a_);
    __println("sw1.b_[11]=" + sw1.b_);
    __println("sw2.a_[10]=" + sw2.a_);
    __println("sw2.b_[21]=" + sw2.b_);
    bool sok_a = (sw1.p_ == ^ys1);
    bool sok_b = (sw2.p_ == ^xs1);
    __println("sok_a[1]=" + sok_a);
    __println("sok_b[1]=" + sok_b);

    /* swap class without overload */
    int xn1 = 10000;
    int yn1 = 20000;
    NoSwap n1(30, 31, ^xn1);
    NoSwap n2(40, 41, ^yn1);
    n1 <-> n2;
    __println("n1.a_[40]=" + n1.a_);
    __println("n1.b_[41]=" + n1.b_);
    __println("n2.a_[30]=" + n2.a_);
    __println("n2.b_[31]=" + n2.b_);
    bool nok_a = (n1.p_ == ^yn1);
    bool nok_b = (n2.p_ == ^xn1);
    __println("nok_a[1]=" + nok_a);
    __println("nok_b[1]=" + nok_b);

    /* side effects */
    int arr[4] = (100, 200, 300, 400);
    int[] p1 = ^arr[0];
    int[] p2 = ^arr[3];
    p1++^ <-> p2--^;
    bool b1 = (p1 == ^arr[1]);
    bool b2 = (p2 == ^arr[2]);
    __println("arr[400,200,300,100]=(" + arr[0] + "," + arr[1] + "," + arr[2] + "," + arr[3] + ")");
    __println("b1[1]=" + b1);
    __println("b2[1]=" + b2);

    /* ----------------------------------------------------------
       SwapStmt lvalue gap — POSITIVE tests.
       These are the spec for the next compiler change. They fail
       to compile today with "SwapStmt: unsupported lvalue shape";
       the codegen fix grows resolveLvalue's bare-DerefExpr arm
       and they go green.
       ---------------------------------------------------------- */

    /* bare ptr^ <-> ptr^ — value swap through two int^ references. */
    {
        int xa = 1;
        int xb = 2;
        int^ ra = ^xa;
        int^ rb = ^xb;
        ra^ <-> rb^;
        __println("xa[2]=" + xa);
        __println("xb[1]=" + xb);
    }

    /* same shape, char[] iterators — the string.sl reverse() pattern. */
    {
        char buf[4] = ('a', 'b', 'c', 'd');
        char[] lo = ^buf[0];
        char[] hi = ^buf[3];
        lo^ <-> hi^;
        __println("buf[dbca]=(" + buf[0] + "," + buf[1] + "," + buf[2] + "," + buf[3] + ")");
    }

    /* mixed PostIncDeref + bare DerefExpr — one side advances, other doesn't. */
    {
        int arr2[2] = (1, 2);
        int[] ia = ^arr2[0];
        int[] ib = ^arr2[1];
        ia++^ <-> ib^;
        __println("arr2[2,1]=(" + arr2[0] + "," + arr2[1] + ")");
        bool ma = (ia == ^arr2[1]);
        bool mb = (ib == ^arr2[1]);
        __println("ma[1]=" + ma);
        __println("mb[1]=" + mb);
    }

    /* obj.x <-> obj.y on a value local — direct FieldAccessExpr.
       falls out of the recursive resolver. */
    {
        Swap sw(7, 9, nullptr);
        sw.a_ <-> sw.b_;
        __println("sw.a_[9]=" + sw.a_);
        __println("sw.b_[7]=" + sw.b_);
    }

    /* ----------------------------------------------------------
       NEGATIVE tests — //-EXPECT-ERROR markers drive
       test/run_negatives.sh.
       ---------------------------------------------------------- */

    /* deferred — parser does not yet accept (--p1)^ / (++p2)^ as swap operands.
       separate from the SwapStmt codegen fix above. */
    //-EXPECT-ERROR-DEFERRED: parser does not yet accept (--p1)^ / (++p2)^ as swap operands
    //{
    //    p1 = ^arr[3];
    //    p2 = ^arr[0];
    //    (--p1)^ <-> (++p2)^;
    //    bool b1b = (p1 == ^arr[2]);
    //    bool b2b = (p2 == ^arr[1]);
    //    __println("arr[400,300,200,100]=(" + arr[0] + "," + arr[1] + "," + arr[2] + "," + arr[3] + ")");
    //    __println("b1b[1]=" + b1b);
    //    __println("b2b[1]=" + b2b);
    //}

    /* always-error — type mismatch (int vs bool). */
    //-EXPECT-ERROR: SwapStmt: type mismatch
    //{
    //    int xv = 1;
    //    bool bv = true;
    //    xv <-> bv;
    //}

    return 0;
}
