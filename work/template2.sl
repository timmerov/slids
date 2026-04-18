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

    /* test mismatches */

    int8 i8 = 8;
    int32 i32 = 32 + 1000;
    should_be_int32 = add(i8, i32);
    println(String + "should_be_int32[1040]=" + should_be_int32);

    String s = "y[42]=";
    int y = 42;
    s = add(s, y);
    println(s);

    s = "=y[42]";
    s = add(y, s);
    println(s);

    return 0;
}
