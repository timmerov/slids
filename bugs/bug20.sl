/*
debate: type conversion of pointers.
*/

Class(int x_ = 0) {
    op=(Class^ rhs) {
        __println("Class:copy");
    }
}

Derived(int y_ = 1) {
    void foo(Class^ rhs) {
        /* copy the base class. */

        /* approved method. */
        Class:self = rhs^;

        /* cast method. */
        clsp = <Class^> ^self;
        clsp^ = rhs^;

        /* cryptic method. */
        (<Class^> ^self)^ = rhs^;

        /*
        should this be allowed?
        we are using type conversion syntax
        to reinterpret cast pointers.
        that seems like a violation of
        syntax equals semantics.
        */
        (Class^ = ^self)^ = rhs^;

        /* scared to try this.*/
        (Class ^=^ self)^ = rhs^;
    }
}

int32 main() {

    Class cls(2);
    Derived der(3, 4);
    der.foo(^cls);

    return 0;
}
