
Simple(
    int x_ = 0,
    int y_ = 0,
    int z_ = 0
) {
    /* constructor/destructor */
    _() {
        __println("constructor");
    }
    ~() {
        __println("destructor");
    }

    void print() {
        __println("Simple: x=" + x_ + " y=" + y_ + " z=" + z_);
    }
}

Template<T>() {
    void test() {
        int8 buff[100];
        int8[] buffer = ^buff[0];
        //T^ discard = new(buffer) T;
    }
}

int32 main() {

    /* create a single object. */
    __println("new Simple;");
    one = new Simple(1, 2, 3);
    one^.print();
    delete one;

    /* create an array of objects. */
    __println("new Simple[3];");
    many = new Simple[3];
    delete many;

    /* create a single object in place. */
    __println("placement new");
    /* allocate space for the object. */
    size = sizeof(Simple);
    where = new int8[size];
    /* construct the object. */
    inplace = new(where) Simple(10, 11, 12);
    inplace^.print();
    /* destruct the object. */
    inplace^.~();
    /* free the memory. */
    delete where;

    Template<Simple> tpt;
    tpt.test();

    return 0;
}
