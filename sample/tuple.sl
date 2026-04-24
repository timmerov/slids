/*
test tuple usage.
*/

Simple(
    int x_ = 0,
    int y_ = 1,
    int z_ = 2
) {
    void print(char[] name) {
        __println(name + ": (x,y,z)=(" + x_ + "," + y_ + "," + z_ + ")");
    }
    op+(Simple^ a, Simple^ b) {
        x_ = a^.x_ + b^.x_;
        y_ = a^.y_ + b^.y_;
        z_ = a^.z_ + b^.z_;
    }
    op*(Simple^ a, Simple^ b) {
        x_ = a^.x_ * b^.x_;
        y_ = a^.y_ * b^.y_;
        z_ = a^.z_ * b^.z_;
    }
}

Counted(int c_) {
    op=(Counted^ rhs) {
        __println("Counted:op=");
        c_ = rhs^.c_;
    }
}

IntBuf(int[] data_ = nullptr) {}

Action(
    int x_
) {
    _() {
        __println("Action:ctor");
    }
    ~() {
        __println("Action:dtor");
    }

    op<-(Action^ rhs) {
        __println("Action:move");
    }
}

NameValue(char[] name_, int value_) {}

void foo(NameValue^ nv) {
    __println("foo:print: " + nv^.name_ + " = " + nv^.value_);
}

void takeAction(Action^ a) {
    __println("takeAction received");
}

(int a, int b, int c) make_tuple() {
    return (100, 200, 300);
}

(Simple s0, Simple s1) make_simples() {
    return (Simple(1, 2, 3), Simple(4, 5, 6));
}

(int t0, int t1, int t2) make_tuple_var() {
    t = (11, 22, 33);
    return t;
}

(Simple s0, Simple s1) make_simples_from_locals() {
    Simple a(7, 8, 9);
    Simple b(10, 11, 12);
    return (a, b);
}

(Simple s0, Simple s1) make_simples_var() {
    pair = (Simple(13, 14, 15), Simple(16, 17, 18));
    return pair;
}

(Action a0, Action a1) make_actions_move() {
    return (Action(0), Action(1));
}

(Action a0, Action a1) make_actions_copy() {
    Action a(0);
    Action b(1);
    return (a, b);
}

(Simple s0, Simple s1) passthrough() {
    return make_simples_var();
}

int32 main() {

    /* assignments with tuples. */

    Simple a;
    a.print("a");

    Simple b(10, 11, 12);
    b.print("b");

    Simple c = (20, 21, 22);
    c.print("c");

    Simple d(30, 31, 32) = (40, 41, 42);
    d.print("d");

    d = (50, 51, 52);
    d.print("d*");

    d <- (60, 61, 62);
    d.print("d<-");

    /* compile errors. */
    //Simple e(1, 2, 3, 4, 5);
    //d = (1, 2, 3, 4, 5);

    /* creating accessing. */
    tuple = (1, 2, 3);
    one = tuple[0];
    two = tuple[1];
    tuple[2] = 10;
    ten = tuple[2];
    __println("tuple = (" + one + "," + two + "," + ten + ")");
    tuple2 = tuple;
    __println("tuple2 = (" + tuple2[0] + "," + tuple2[1] + "," + tuple2[2] + ")");
    tuple3 <- tuple;
    __println("tuple3 = (" + tuple3[0] + "," + tuple3[1] + "," + tuple3[2] + ")");
    (int, int, int) tuple4 <- tuple;
    __println("tuple4 = (" + tuple4[0] + "," + tuple4[1] + "," + tuple4[2] + ")");

    /* overwriting */
    tuple2 = (100, 200);
    __println("tuple2 = (" + tuple2[0] + "," + tuple2[1] + "," + tuple2[2] + ")");
    tuple = tuple2;
    __println("tuple = (" + tuple[0] + "," + tuple[1] + "," + tuple[2] + ")");

    /* compile errors */
    //tuple = (1, 2, 3, 4, 5);
    //tuple = (1, "Hello", 3);
    big_tuple = (1, 2, 3, 4, 5);
    //tuple = big_tuple;
    wrong_tuple = (1, "Hello", 3);
    //tuple = wrong_tuple;

    /* destructuring. */
    (int x, int y) = (-1, -2);
    __println("x=" + x + " y= " + y);
    (a1, b1) = (42, "weird");
    __println("a1=" + a1 + " b1=" + b1);
    weird_tuple = (42, "weird");
    (c1, ) = weird_tuple;
    (, d1) = weird_tuple;
    __println("c1=" + c1 + " d1=" + d1);

    /* slid tuples. */
    slid_tuple = (a, b);
    slid_tuple[0].print("slid_tuple[0]");
    slid_tuple[1].print("slid_tuple[1]");
    slid_tuple = (Simple(1,2,3), Simple(4,5,6));
    slid_tuple[0].print("slid_tuple[0]");
    slid_tuple[1].print("slid_tuple[1]");
    slid_tuple[0] = Simple(7, 8, 9);
    slid_tuple[0].print("slid_tuple[0] after element-write");
    (Simple p, Simple q) = slid_tuple;
    p.print("p");
    q.print("q");

    /* constructables. */
    {
        __println("6 ctor, 2 move, 6 dtor.");
        xtor_tuple = (Action(0), Action(2));
        xtor1_tuple = xtor_tuple;
        xtor_tuple = xtor1_tuple;
        xtor1_tuple <- xtor_tuple;
    }

    /* functions */
    /*NameValue nv("x", 42);
    foo(^nv);
    foo(nv);
    */

    /* tuple return from function. */
    ret_tuple = make_tuple();
    __println("ret_tuple = (" + ret_tuple[0] + "," + ret_tuple[1] + "," + ret_tuple[2] + ")");
    ret_tuple2 <- make_tuple();
    __println("ret_tuple2 = (" + ret_tuple2[0] + "," + ret_tuple2[1] + "," + ret_tuple2[2] + ")");

    slid_ret = make_simples();
    slid_ret[0].print("slid_ret[0]");
    slid_ret[1].print("slid_ret[1]");

    var_ret = make_tuple_var();
    __println("var_ret = (" + var_ret[0] + "," + var_ret[1] + "," + var_ret[2] + ")");

    local_ret = make_simples_from_locals();
    local_ret[0].print("local_ret[0]");
    local_ret[1].print("local_ret[1]");

    var_slid_ret = make_simples_var();
    var_slid_ret[0].print("var_slid_ret[0]");
    var_slid_ret[1].print("var_slid_ret[1]");

    /* Action-return tests: expect 8 ctor, 2 move, 8 dtor. */
    {
        __println("-- action move return: expect 2 move lines --");
        act_move_ret = make_actions_move();
        __println("-- action copy return: expect 0 move lines --");
        act_copy_ret = make_actions_copy();
    }

    pass_ret = passthrough();
    pass_ret[0].print("pass_ret[0]");
    pass_ret[1].print("pass_ret[1]");

    /* element-wise tuple binary ops. */
    sum_tuple = (1, 2, 3) + (4, 5, 6);
    __println("sum = (" + sum_tuple[0] + "," + sum_tuple[1] + "," + sum_tuple[2] + ")");
    diff_tuple = (10, 20, 30) - (1, 2, 3);
    __println("diff = (" + diff_tuple[0] + "," + diff_tuple[1] + "," + diff_tuple[2] + ")");
    prod_tuple = sum_tuple * (2, 3, 4);
    __println("prod = (" + prod_tuple[0] + "," + prod_tuple[1] + "," + prod_tuple[2] + ")");

    /* element-wise slid tuple ops — dispatches Simple::op+ per slot. */
    ss = (Simple(1,2,3), Simple(4,5,6)) + (Simple(10,20,30), Simple(40,50,60));
    ss[0].print("ss[0]");
    ss[1].print("ss[1]");

    /* tuple[N] <- val move: dispatches op<- on the slot's slid type. */
    {
        __println("-- tuple[N] <- move test: expect 5 ctor, 1 move, 5 dtor --");
        at = (Action(0), Action(1));
        at[0] <- Action(2);
    }

    /* single-slid Simple::op+ dispatch (bare, not inside a tuple). */
    sA = Simple(1, 2, 3);
    sB = Simple(10, 20, 30);
    sC = sA + sB;
    sC.print("sC");

    /* elementwise Simple::op* on slid tuples. */
    sq = (Simple(1,2,3), Simple(4,5,6)) * (Simple(10,10,10), Simple(100,100,100));
    sq[0].print("sq[0]");
    sq[1].print("sq[1]");

    /* op= dispatch via emitSlidSlotAssign on per-slot tuple-literal init. */
    {
        __println("-- op= dispatch test: expect 2 Counted:op= prints --");
        cA = Counted(1);
        cB = Counted(2);
        ct = (cA, cB);
    }

    /* #17: raw-pointer base[idx] <- val null-outs the source when elt is indirect.
       Also exercises string-literal inside new Type(args) (collectStringConstants fix). */
    {
        __println("-- raw-pointer move test --");
        arr = new NameValue^[2];
        src = new NameValue("hello", 42);
        arr[0] <- src;  /* null-outs src */
        if (src == nullptr) {
            __println("src is null after move");
        }
        delete arr[0];
        delete arr;
    }

    /* #5 copy: chained indexed field lvalue — tuple[N].field = val. */
    slid_tuple[0].x_ = 99;
    slid_tuple[0].print("after slid_tuple[0].x_ = 99");

    /* #5 move + #16 observable: chained indexed field move nulls pointer source.
       Also exercises chained indexed rvalue read via `delete ib_tup[0].data_`. */
    {
        __println("-- chained indexed field move test --");
        buf = new int[3];
        ib_tup = (IntBuf(), IntBuf());
        ib_tup[0].data_ <- buf;
        if (buf == nullptr) {
            __println("buf is null after move into ib_tup[0].data_");
        }
        delete ib_tup[0].data_;
    }

    /* #6: nested anonymous tuples. emitExpr(TupleExpr) now materializes a nested
       tuple literal. Read back via `inner = nested[1]` (non-literal VarDecl path). */
    nested = (1, (2, 3));
    __println("nested[0]=" + nested[0]);
    inner = nested[1];
    __println("inner=(" + inner[0] + "," + inner[1] + ")");

    /* #14: elementwise ops on nested anon-tuple slots (recurses via emitElementwiseAtPtr). */
    ntx = ((1,2),(3,4)) + ((5,6),(7,8));
    nt0 = ntx[0];
    nt1 = ntx[1];
    __println("ntx = ((" + nt0[0] + "," + nt0[1] + "),(" + nt1[0] + "," + nt1[1] + "))");

    /* #3: slid temp in non-consume context (call arg). Expect 1 ctor, 1 dtor. */
    {
        __println("-- slid temp in call arg test --");
        takeAction(Action(99));
    }

    return 0;
}
