/*
define how function templates work in a source file.
*/

import string

T add<T>(T a, T b) {
    return a + b;
}

int32 main() {
    x = add<int>(2, 4);
    String s = "x[6]=" + x;
    s.println();

    intptr a = 0x1234;
    intptr b = 0x5678;
    c = add<intptr>(a, b);
    s = "c[26796]=" + c;
    s.println();

    /*String hello = "Hello, ";
    String world = "World!";
    greeting = add<String>(hello, world);
    s = "greeting[Hello, World!]=" + greeting;
    s.println();*/

    return 0;
}
