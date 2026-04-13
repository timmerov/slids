/*
Opaque class implementation.
This class has private fields not declared in the header files.
*/

/* declare the private fields. */
Opaque(
    int x_ = 42
) {
    void printSecretMessage() {
        __println("The answer is: " + x_ + ".");
    }
}
