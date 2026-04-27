/*
dump prints the type name and value of a variable.
uses the stringification operator #.
*/

import string;
import dump;

/*
usage:
    int x = 42;
    dump(#x);
output:
    int x = 42
*/
void dump<T>(
    (char[], char[], T^)^ tuple
) {
    println(String + tuple^[0] + " " + tuple^[1] + "=" + tuple^[2]^);
}

/*
usage:
    int x = 42;
    dump(#x, "37");
output:
    int x = 42 : 37;
*/
void dump<T>(
    (char[], char[], T^)^ tuple,
    char[] expected
) {
    println(String + tuple^[0] + " " + tuple^[1] + "=" + tuple^[2]^ + ":" + expected);
}
