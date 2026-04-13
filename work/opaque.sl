/*
Opaque class implementation.

this class has private fields not declared in the header file.
when this file is compiled it also writes as an output build/opaque.slh.
which contains a line for line copy of opaque.slh but the Opaque
class is size annotated.
*/

/* imports the not-annotated version ./opaque.slh */
import opaque;

/*
the ... indicates there are public fields defined elsewhere.
declare the private fields after.
*/
Opaque(
    ...,
    int y_ = 42
) {
    void printSecretMessage() {
        __println("The answer is: " + y_ + ", not " + x_ + ".");
    }
}
