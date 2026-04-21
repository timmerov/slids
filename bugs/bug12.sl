/*
redefined constructors.
*/

Simple() {
    /* first definitions. */
    _() {}
    ~() {}
    void foo() {}

    /* second definitions. */
    _() {
        __println("Simple:ctor");
    }
    ~() {
        __println("Simple:dtor");
    }
    /* this one correctly fails. */
    /*void foo() {
        __println("Simple:foo:");
    }*/
}

int32 main() {
    Simple s;
    s.foo();

    return 0;
}
