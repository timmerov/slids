/*
import declarations from a header file.
use the declared fuction.
link with the compiled implementation.
*/

import string;

int32 main() {

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
    String s5 <-- s4;
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

    /* comparisons. */
    beq = (s1 == s2);
    bne = (s1 != s2);
    bge = (s1 >= s2);
    ble = (s1 <= s2);
    bgt = (s1 > s2);
    blt = (s1 < s2);
    println(String + "beq=" + beq);
    println(String + "bne=" + bne);
    println(String + "bge=" + bge);
    println(String + "ble=" + ble);
    println(String + "bgt=" + bgt);
    println(String + "blt=" + blt);

    /* api tests. */
    println(String + "s1.size()<13>=" + s1.size());
    println(String + "s1.empty()<false>=" + s1.empty());

    /* indexing. */
    ch = s1[1];
    println(String + "s1[1]<'e'>=" + ch);
    s1[1] = 'E';
    println(String + "s1<HEllo, World!>" + s1);

    /* for by reference */
    String s6 = "ABC";
    for (char^ it : s6) {
        println(String + "it^<ABC>=" + it^);
    }

    /* for by value */
    String s7 = "XYZ";
    for (char ch : s7) {
        println(String + "ch<XYZ>=" + ch);
    }

    /* slicing */
    String s8 = s2.slice(7, 5);
    println(String + "s8<World>=" + s8);

    /* exercise formatting strings. */
    Format fmt;
    fmt.justify_ = Format:kLeftJustify;
    fmt.pad_ = '.';
    fmt.min_width_ = 16;
    fmt = s2;
    println(String + "left : [" + fmt + "]");
    fmt.clear();
    fmt.justify_ = Format:kRightJustify;
    fmt = s2;
    println(String + "right: [" + fmt + "]");
    fmt.clear();
    fmt.max_width_ = 10;
    fmt = s2;
    println(String + "max_r: [" + fmt + "]");
    fmt.clear();
    fmt.justify_ = Format:kLeftJustify;
    fmt = s2;
    println(String + "max_l: [" + fmt + "]");
    fmt.clear();
    fmt.max_width_ = -1;
    fmt.justify_ = Format:kRightJustify;
    fmt = "Howdy!";
    println(String + "char[] =[" + fmt + "]");
    fmt.clear();
    fmt = 'A';
    println(String + "char   =[" + fmt + "]");
    fmt.clear();
    fmt = -123456789;
    println(String + "int64  =[" + fmt + "]");
    fmt.clear();
    fmt = (uint64=-123456789);
    println(String + "uint64 =[" + fmt + "]");
    fmt.clear();
    fmt = true;
    println(String + "bool   =[" + fmt + "]");
    fmt.clear();
    fmt = 3.14;
    println(String + "float  =[" + fmt + "]");
    fmt.clear();
    fmt = (float32=3.14);
    println(String + "float32=[" + fmt + "]");
    fmt.clear();
    fmt = (float64=3.14);
    println(String + "float64=[" + fmt + "]");
    fmt.clear();
    fmt.leading_plus_ = true;
    fmt = 1337;
    println(String + "int    =[" + fmt + "]");
    fmt.clear();
    fmt.justify_ = Format:kLeftJustify;
    fmt = 2.78;
    println(String + "float  =[" + fmt + "]");
    fmt.clear();
    fmt.float_style_ = Format:kFixedPoint;
    fmt = 99.44;
    println(String + "fixed  =[" + fmt + "]");
    fmt.clear();
    fmt.float_style_ = Format:kScientificNotation;
    fmt.precision_ = 4;
    fmt = 20.26;
    println(String + "SciNot =[" + fmt + "]");

    fmt.float_style_ = Format:kFixedPoint;
    fmt.justify_ = Format:kRightJustify;
    println(String + "factor =[" + fmt*1234.56 + "]");

    Format fmt1;
    fmt1.justify_ = Format:kLeftJustify;
    fmt1.min_width_ = 8;
    Format fmt2;
    fmt2.justify_ = Format:kRightJustify;
    fmt2.min_width_ = 8;
    println(String + "2 fmts =[" + fmt1*123 + fmt2*4.56 + "]");

    return 0;
}
