/*
built-in dtors.
*/

Value1(
    int a_ = 0
) {
    _() {
        __println("Value1:ctor");
    }
    ~() {
        __println("Value1:dtor");
    }
}

Value2(
    Value1 value_
) {
    _() {
        __println("Value2:ctor");
    }
    ~() {
        __println("Value2:dtor");
    }
}

int32 main() {

    {
        int x = 42;
        x.~();
        __println("int has a dtor.");
    }

    {
        int^ p = nullptr;
        p.~();
        __println("int^ has a dtor.");
    }

    {
        __println("expected v1:ctor v2:ctor v2:dtor v1:dtor");
        Value2 value2;
    }

    {
        __println("expected v1:ctor x2 v1:dtor x3");
        Value1 value = 37;
        tuple = (42, value);
        /* tuple[1].a_ is not yet supported. */
        __println("(" + tuple[0] + "," + "tbd" + ")");
        tuple.~();
    }

    {
        __println("expected ctors:1212 dtors:212121");
        Value2 value = 37;
        tuple = (42, value);
        __println("(" + tuple[0] + "," + "tbd" + ")");
        tuple.~();
    }

    return 0;
}
