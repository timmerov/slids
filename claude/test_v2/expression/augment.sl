/*
test implementation of expressions.

test augmented assignments.

unary: + - ~ !
binary:
    math: + - * / %
    bitwise: & | ^ << >>
    logical: && || ^^
    comparison: == != >= <= > <

precedence, parentheses, ...

! and comparisons consume condition-expressions.
0-like values (false, 0, 0.0, nullptr) are false.
everything else is true.

pre/post increment/decrement not tested here.
the ppid rule has implications for parameters.

augmented assignments: += -= *= /= %= &= |= ^= <<= >>= &&= ||= ^^=
augmented assignments are handled in the desugar stage.
*/

int32 main() {

    // -- signed int arith --
    int32 xi32 = 100;  __println("xi32= " + xi32);
    xi32 += 5;         __println("xi32 +=  5 → " + xi32);  // 105
    xi32 -= 3;         __println("xi32 -=  3 → " + xi32);  // 102
    xi32 *= 2;         __println("xi32 *=  2 → " + xi32);  // 204
    xi32 /= 4;         __println("xi32 /=  4 → " + xi32);  // 51 (sdiv)
    xi32 %= 5;         __println("xi32 %=  5 → " + xi32);  // 1  (srem)

    // -- unsigned int arith (udiv / urem differ from signed) --
    uint32 xu32 = 100;  __println("xu32= " + xu32);
    xu32 /= 3;          __println("xu32 /=  3 → " + xu32);  // 33 (udiv)
    xu32 %= 5;          __println("xu32 %=  5 → " + xu32);  //  3 (urem)

    // -- float arith (f* instrs) --
    float32 xf32 = 10.0;  __println("xf32= " + xf32);
    xf32 += 1.5;          __println("xf32 += 1.5 → " + xf32);  // 11.5 (fadd)
    xf32 *= 2.0;          __println("xf32 *= 2.0 → " + xf32);  // 23.0 (fmul)
    xf32 -= 3.0;          __println("xf32 -= 3.0 → " + xf32);  // 20.0 (fsub)
    xf32 /= 4.0;          __println("xf32 /= 4.0 → " + xf32);  //  5.0 (fdiv)
    xf32 %= 1.5;          __println("xf32 %= 1.5 → " + xf32);  //  0.5 (frem: 5.0 % 1.5)
    xf32 <<= 1;           __println("xf32 <<= 1 → " + xf32);   //  1.0 (0.5 * (1<<1))

    // -- bitwise --
    int32 xbits = 12;  __println("xbits= " + xbits);
    xbits &= 10;       __println("xbits &= 10 → " + xbits);  //  8 (1100 & 1010 = 1000)
    xbits |= 5;        __println("xbits |=  5 → " + xbits);  // 13 (1000 | 0101 = 1101)
    xbits ^= 6;        __println("xbits ^=  6 → " + xbits);  // 11 (1101 ^ 0110 = 1011)

    // -- shift (signed >>= uses ashr, unsigned >>= uses lshr) --
    int32 xshift = 8;   __println("xshift= " + xshift);
    xshift <<= 2;       __println("xshift <<= 2 → " + xshift);  // 32 (shl)
    xshift >>= 1;       __println("xshift >>= 1 → " + xshift);  // 16 (ashr)

    uint32 xushift = 8;  __println("xushift= " + xushift);
    xushift >>= 1;       __println("xushift >>= 1 → " + xushift);  // 4 (lshr)

    // -- logical (bool LHS — result type already matches) --
    bool xb = true;   __println("xb= " + xb);
    xb ||= false;     __println("xb ||= false → " + xb);  // true  || false = true
    xb ^^= true;      __println("xb ^^= true  → " + xb);  // true  ^^ true  = false
    xb &&= true;      __println("xb &&= true  → " + xb);  // false && true  = false

    // -- logical with non-bool LHS — bool result widens (zext) to lvalue width --
    int32 xint_l = 0;  __println("xint_l= " + xint_l);
    xint_l ||= 5;      __println("xint_l ||= 5 → " + xint_l);  // 1 (0 || 5 = true; zext to i32)

    // -- negative augassigns --
    // Setup vars used only by the negatives below; declared so the //-EXPECT-ERROR
    // runner's uncommented lines find them in scope.
    int8   xi8n  = 1;     __println("xi8n= "  + xi8n);
    int16  xi16n = 2;     __println("xi16n= " + xi16n);
    uint64 xu64n = 3;     __println("xu64n= " + xu64n);

    //-EXPECT-ERROR: No common type for 'float32' and 'int32'; use an explicit type conversion.
    // xf32 &= 7;
    //-EXPECT-ERROR: Cannot implicitly narrow 'int32' to 'int16'; use an explicit type conversion.
    // xi16n += xi32;
    //-EXPECT-ERROR: Cannot implicitly convert 'int32' to 'bool'; use an explicit type conversion.
    // xb += 5;
    //-EXPECT-ERROR: No common type for 'uint64' and 'int8'; use an explicit type conversion.
    // xu64n += xi8n;

    return 0;
}
