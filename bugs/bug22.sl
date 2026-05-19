/*
nested functions.
*/


int32 main() {
    {
        void foo() {
            __println("Hello, World!");
        }
        /* this should compile. */
        foo();
        /* this should also compile. */
        bar();
        void bar() {
            __println("Goodbye, World!");
        }
    }
    /* this should not compile: no foo in this scope. */
    //foo();

    return 0;
}
