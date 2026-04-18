/*
define how function templates work in a source file.
*/

import string

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

    return 0;
}
