/*
define how function templates work in a source file.

template type is inferred.
*/

import string;

T add<T>(T a, T b) {
    return a + b;
}

int32 main() {
    x = add(2, 4);
    println(String + "x[6]=" + x);

    intptr a = 0x1234;
    intptr b = 0x5678;
    c = add(a, b);
    println(String + "c[26796]=" + c);

    String hello = "Hello, ";
    String world = "World!";
    greeting = add(hello, world);
    println(String + "greeting[Hello, World!]=" + greeting);

    return 0;
}
