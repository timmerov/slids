/*
define how function templates work
with separate header and source files.
*/

import string;

import template_add;

int32 main() {
    x = add<int>(2, 4);
    println(String + "x[6]=" + x);

    return 0;
}
