/*
test the handling of numeric literals.

lex emits +/- as tokens. they are not folded into a numeric constant.
all numeric literals are positive.

lex emits: char, int, uint, float kinds.
with each, lex emits a string.
underscores are removed.

for char:
ensure exactly 1 character.
handle escaped characters.

for hex and binary:
ensure formed correctly.
0x 0b are compile errors.
ensure no overflow.

for int:
ensure no overflow.
integers larger the 1<<63 change type to uint.

for floats:
convert to standard ieee notation for 64 bits.
ensure no overflow.
*/

/*
forms covered:
  decimal int — digits, with _ separators
  hex         — 0x / 0X, with _ separators
  binary      — 0b / 0B, with _ separators
  float dot   — N.N, with _ separators on either side
  float exp   — e / E, optional +/-, with _ separators

malformed exponent cases follow the //-EXPECT-ERROR pattern.
*/

int32 main() {

    // -- decimal int --
    int d1 = 0;                __println("d1= " + d1);
    int d2 = 123;              __println("d2= " + d2);
    int d3 = 1_000_000;        __println("d3= " + d3);

    // -- hex (uint literal) --
    uint h1 = 0xFF;            __println("h1= " + h1);
    uint h2 = 0XFF;            __println("h2= " + h2);
    uint h3 = 0xDEAD_BEEF;     __println("h3= " + h3);
    uint h4 = 0xabcdef;        __println("h4= " + h4);

    // -- binary (uint literal) --
    uint b1 = 0b1010;          __println("b1= " + b1);
    uint b2 = 0B1010;          __println("b2= " + b2);
    uint b3 = 0b1111_0000;     __println("b3= " + b3);

    // -- float, dot form --
    float64 f1 = 3.14;         __println("f1= " + f1);
    float64 f2 = 3.0;          __println("f2= " + f2);
    float64 f3 = 1_000.5;      __println("f3= " + f3);
    float64 f4 = 3.1_4;        __println("f4= " + f4);

    // -- float, exponent form --
    float64 e1 = 1e2;          __println("e1= " + e1);
    float64 e2 = 1E2;          __println("e2= " + e2);
    float64 e3 = 1e+2;         __println("e3= " + e3);
    float64 e4 = 1e-2;         __println("e4= " + e4);
    float64 e5 = 2.5e3;        __println("e5= " + e5);
    float64 e6 = 1e1_0;        __println("e6= " + e6);

    // -- trailing _ accepted --
    int t1 = 123_;             __println("t1= " + t1);
    uint t2 = 0xFF_;           __println("t2= " + t2);
    uint t3 = 0b1010_;         __println("t3= " + t3);
    float64 t4 = 3.14_;        __println("t4= " + t4);
    float64 t5 = 1e10_;        __println("t5= " + t5);

    // -- char literals --
    char c1 = 'A';             __println("c1= " + c1);
    int  c2 = '\0';            __println("c2= " + c2);
    int  c3 = '\t';            __println("c3= " + c3);
    int  c4 = '\n';            __println("c4= " + c4);
    int  c5 = '\\';            __println("c5= " + c5);
    int  c6 = '\'';            __println("c6= " + c6);

    // -- string literals: the TYPE is `const char[N]` --
    // a string literal is STORAGE, not a pointer: N bytes in the constant pool, N
    // counting the terminating NUL. So its type carries its LENGTH, and every literal
    // of a different length is a different type. (What it converts INTO — the decay to
    // `char[]` at a parameter — is pointer/iterator.sl; the byte size is misc/sizeof.sl.)
    char s1[6] = "hello";      __println("s1= " + ##type(s1));   // char[6]
    char s2[1] = "";           __println("s2= " + ##type(s2));   // char[1]
    // an ESCAPE is one byte, so the length is the DECODED length, not the spelling's.
    char s3[2] = "\0";         __println("s3= " + ##type(s3));   // char[2]
    char s4[2] = "\\";         __println("s4= " + ##type(s4));   // char[2]
    char s5[3] = "\t\n";       __println("s5= " + ##type(s5));   // char[3]

    //-EXPECT-ERROR: Malformed exponent: expected a digit
    // float bad1 = 1e;
    //-EXPECT-ERROR: Malformed exponent: expected a digit
    // float bad2 = 1e+;
    //-EXPECT-ERROR: Malformed exponent: expected a digit
    // float bad3 = 1e-;

    //-EXPECT-ERROR: Unknown escape sequence: '\q'.
    // char bad_esc = '\q';
    //-EXPECT-ERROR: Empty character literal.
    // char bad_empty = '';
    //-EXPECT-ERROR: Character literal must be a single byte.
    // char bad_multi = 'AB';
    //-EXPECT-ERROR: Character literal must be a single byte.
    // char bad_esc_multi = '\tA';

    //-EXPECT-ERROR: Hex literal missing digits.
    // uint bad_hex_empty = 0x;
    //-EXPECT-ERROR: Binary literal missing digits.
    // uint bad_bin_empty = 0b;

    //-EXPECT-ERROR: Hex literal overflows uint64.
    // uint bad_hex_big = 0x1_0000_0000_0000_0000;
    //-EXPECT-ERROR: Binary literal overflows uint64.
    // uint bad_bin_big = 0b1_0000000000_0000000000_0000000000_0000000000_0000000000_0000000000_0000;
    //-EXPECT-ERROR: Integer literal overflows uint64.
    // int bad_int_big = 18_446_744_073_709_551_616;
    //-EXPECT-ERROR: Float literal overflows float64.
    // float bad_float_big = 1e400;

    return 0;
}
