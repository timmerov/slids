/*
rudimentary mutable keyword.

it's required for move and swap overloads.
that's tested elsewhere.

mutable applies only to a pointer passed to a
function or method.
*/

/* check methods. */
Class(int x_ = 0) {

    /* correct syntax */
    void copy1(mutable int8[] dst, int8[] src) { }
    void copy2(mutable Class^ dst, Class^ src) { }

    /* compile error: 'mutable' on non-pointer param. */
    // void wrong1(mutable int x) { }
}

/* correct syntax */
void copy3(mutable int8[] dst, int8[] src) { }
void copy4(mutable Class^ dst, Class^ src) { }

/* compile error: 'mutable' on non-pointer param (free function). */
// void wrong2(mutable int x) { }

/* check template methods. */
Box<T>(T value_ = 0) {
    /* correct syntax */
    op<-(mutable Box<T>^ other) {
        __println("Box<T>:op<-(mutable Box<T>^)");
    }
}

BadBox<T>(T value_ = 0) {
    /* compile error: missing 'mutable' on op<- pointer param in template. */
    // op<-(BadBox<T>^ other) { }
}

int32 main() {
    /* exercise positive template instantiation. */
    Box<int> bx;
    Box<int> by;
    bx <- by;

    /* compile error verification messages. */
    __println("01: Not allowed: void wrong1(mutable int x) (member, non-pointer)");
    __println("02: Not allowed: void wrong2(mutable int x) (free fn, non-pointer)");
    __println("03: Not allowed: op<-(BadBox<T>^ other) (template missing 'mutable')");

    return 0;
}
