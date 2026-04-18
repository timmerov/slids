/*
define how function templates work in a source file.
*/

import string;

T add<T>(T a, T b) {
    return a + b;
}

int32 main() {
    x = add<int>(2, 4);
    println(String + "x[6]=" + x);

    intptr a = 0x1234;
    intptr b = 0x5678;
    c = add<intptr>(a, b);
    println(String + "c[26796]=" + c);

    String hello = "Hello, ";
    String world = "World!";
    greeting = add<String>(hello, world);
    println(String + "greeting[Hello, World!]=" + greeting);

    /* test mismatches */

    int8 i8 = 8;
    int32 i32 = 32;
    int64 i64 = add<int64>(i8, i32);
    println(String + "i64[40]=" + i64);

    i64 = 64;
    i8 = add<int64>(i32, i64);
    println(String + "i8[96]=" + i8);

    char[] ptr = "y[42]=";
    int y = 42;
    s = add<String>(ptr, y);
    println(s);

    return 0;
}
