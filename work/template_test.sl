/*
define how function templates work
with separate header and source files.
*/

import template_decl;

int32 main() {
    x = add<int>(2, 4);
    println(String + "x[6]=" + x);

    String hello = "Hello, ";
    String world = "World!";
    greeting = add<String>(hello, world);
    println(String + "greeting[Hello, World!]=" + greeting);

    return 0;
}
