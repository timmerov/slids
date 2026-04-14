/*
define how function templates work in a source file.
*/

import string

T add<T>(T a, T b) {
    return a + b;
}

int32 main() {
    x = add<int>(2, 4);
    /*
    it turns out, this is a vexing parse:
    "abc" + 4
    parses as pointer math. ie:
    ^"abc"[4]
    the pointer is beyond the end of the string literal.
    so s is set to be ... something.
    the code segfaults.
    not sure what to do about this.
    claude was flailing.

    i kinda want String:op+(String^, String^) to be invoked.
    cause the given syntax is so nice and simple.
    but why should it be?
    char[] + int
    is a much more rational choice.
    even if we define String:op+(char[], int),
    pointer math is still likely to be invoked.
    hrm...
    sigh.
    */
    String s = "x[6]=" + x;
    s.println();

    intptr a = 0x1234;
    intptr b = 0x5678;
    c = add<intptr>(a, b);
    s = "c[26796]=" + c;
    s.println();

    String hello = "Hello, ";
    String world = "World!";
    greeting = add<String>(hello, world);
    s = "greeting[Hello, World!]=" + greeting;
    s.println();

    return 0;
}
