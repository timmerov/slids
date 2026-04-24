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
}

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
        __println("2 ctor, 2 dtor, 2 move.");
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

    /* element-wise tuple binary ops. */
    sum_tuple = (1, 2, 3) + (4, 5, 6);
    __println("sum = (" + sum_tuple[0] + "," + sum_tuple[1] + "," + sum_tuple[2] + ")");
    diff_tuple = (10, 20, 30) - (1, 2, 3);
    __println("diff = (" + diff_tuple[0] + "," + diff_tuple[1] + "," + diff_tuple[2] + ")");
    prod_tuple = sum_tuple * (2, 3, 4);
    __println("prod = (" + prod_tuple[0] + "," + prod_tuple[1] + "," + prod_tuple[2] + ")");

    return 0;
}
