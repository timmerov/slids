/*
import declarations from a header file.
use the declared fuction.
link with the compiled implementation.
*/

import string;

//void String:expand_other(intptr sz);

int32 main() {

    __println("Output should be:");
    __println("Hello, World!");
    __println("Hello, World!");
    __println("X");
    __println("0");
    __println("137");
    __println("-42");
    __println("-2147483648");
    __println("2147483648");
    __println("-9223372036854775808");
    __println("9223372036854775808");
    __println("");
    __println("9223372036854775808");
    __println("");
    __println("Output is:");

    /* assignment from string literal. */
    String s1 = "Hello, World!";
    println(s1);

    /* assignement from string. */
    String s2 = s1;
    println(s2);

    /* assignment from character. */
    String s3 = 'X';
    println(s3);

    /* assignment from ints. */
    String s4 = 0;
    println(s4);
    s4 = 137;
    println(s4);
    s4 = -42;
    println(s4);
    s4 = int32(0x8000_0000);
    println(s4);
    s4 = uint32(0x8000_0000);
    println(s4);
    s4 = int64(0x8000_0000_0000_0000);
    println(s4);
    s4 = uint64(0x8000_0000_0000_0000);
    println(s4);

    /* move operator. */
    String s5 <- s4;
    println(s4);
    println(s5);

    /* clear. */
    s5.clear();
    println(s5);

    /* reserve works if the above works. */
    /* reverse works if number assignment works. */
    /* strlen works if the above work. */
    /* strcpy works if the above work. */

    /* fancier features. */
    int x = 42;
    println(String + "x=" + x);

    /* link should fail. */
    //s5.expand_other(100);

    return 0;
}
