/*
test the visibility of a global declared in function.
*/

void foo() {
    Bar() {
        global int g_a = 42;
    }

    /* compile error - correct */
    //__println("foo: " + g_a);

    /* compiles - correct */
    __println("foo: " + Bar:g_a);
}

int32 main() {
    /* compile error - correct */
    //int a1 = g_a;

    /*
    this should be a compile error but isn't.
    Bar should not be visible outside of foo.
    */
    int a2 = Bar:g_a;

    foo();
    return 0;
}
