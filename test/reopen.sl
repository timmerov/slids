/*
reopen a class within scoped context.
*/

Class(int x_ = 0) {
    void const foo() {
        __println("Class:foo");
    }
}

void sort<Compare, Container>(Container^ container) {
    result = Compare:less(1.0, 2.0);
    container^.foo();
    __println("result = " + result);
}

int32 main() {

    Class cls;
    Less() {
        bool less(float a, float b) {
            return (a < b);
        }
    }
    //sort<Less, Class>(^cls);

    return 0;
}
