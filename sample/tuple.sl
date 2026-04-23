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
}

NameValue(char[] name_, int value_) {}

void foo(NameValue^ nv) {
    __println("foo:print: " + nv^.name_ + " = " + nv^.value_);
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

    /* constructables. */
    {
        xtor_tuple = (Action(0), Action(2));
    }

    /* functions */
    /*NameValue nv("x", 42);
    foo(^nv);
    foo(nv);
    */

    return 0;
}
