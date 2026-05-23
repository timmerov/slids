/*
define how function templates work
with separate header and source files.
*/

import dump;
import string;
import template_add;

int32 main() {
    x = add(2, 4);
    dump(#x, "6");

    String hello = "Hello, ";
    String world = "World!";
    s = add(hello, world);
    dump(#s, "Hello, World!");

    s = add(hello, x);
    dump(#s, "Hello, 6");

    s = add(x, world);
    dump(#s, "6World!");

    return 0;
}
