/*
test class methods.

functions declared in a class body are methods.
the object fields are accessible within method scope.

*/

/*
claude says:

tbd
*/

Method(int x_) {
    void print() {
        __println("Method:print: " + x_);
    }
}

int32 main() {

    Method method1;
    method1.print();

    return 0;
}
