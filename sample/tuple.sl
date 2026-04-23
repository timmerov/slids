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

    /*d = (50, 51, 52);
    d.print("d*");*/

    /* accessing. */
    tuple = (1, 2, 3);
    one = tuple[0];
    two = tuple[1];
    tuple[2] = 10;
    ten = tuple[2];
    __println("tuple = (" + one + "," + two + "," + ten + ")");

    /* destructuring. */
    /*(int x, int y) = (-1, -2);
    __println("x=" + x + " y= " + y);*/

    /* functions */
    /*NameValue nv("x", 42);
    foo(^nv);
    foo(nv);
    */

    return 0;
}
