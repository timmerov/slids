/*
reopen a class within scoped context.
*/

Class(int x_ = 0) {
    void foo() {
        __println("foo");
    }
}

int32 main() {

    Class cls;
    {
        cls.foo();
        Class() {
            void bar() {
                __println("bar");
            }
        }
        //cls.bar();
    }
    cls.foo();
    //cls.bar();

    return 0;
}
