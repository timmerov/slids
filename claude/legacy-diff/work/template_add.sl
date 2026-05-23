/*
define how function templates work
with separate header and source files.
*/

import template_add;

/* declare the template function. */
T add<T>(T a, T b) {
    return a + b;
}
