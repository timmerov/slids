/*
test implementation of expressions.

simplify to ints only to focus on the parser.

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

/*
claude says:

int/bool only, by design — this file stresses the core operator set without
type-interaction noise. Broader types live in noninttype.sl; mixed types in
mixedtype.sl; precedence in order.sl.

Coverage added this pass: signed negative div/rem/shr (neg7/neg8 -> sDiv/sRem/
sShr, so sdiv/srem/ashr are output-locked, not just IR-verified), shift-by-
variable (shlV/shrV -> two-register shl/ashr), and compare var-vs-literal
(eqAL..geAL).

int is signed i32 throughout (sdiv/srem/ashr, slt..), confirmed against the IR.
Nothing open here.
*/

import string;

int32 main() {

    int u = 5;
    int pu = +u;
    int nu = -u;
    int cu = ~u;
    bool bu = !u;
    println(String + "pu= " + pu);
    println(String + "nu= " + nu);
    println(String + "cu= " + cu);
    println(String + "bu= " + bu);

    int p5 = +5;
    int n5 = -5;
    int c5 = ~5;
    bool b5 = !5;
    println(String + "p5= " + p5);
    println(String + "n5= " + n5);
    println(String + "c5= " + c5);
    println(String + "b5= " + b5);

    int a = 12;
    int b = 5;
    int sum = a + b;
    int dif = a - b;
    int prd = a * b;
    int quo = a / b;
    int rem = a % b;
    println(String + "sum= " + sum);
    println(String + "dif= " + dif);
    println(String + "prd= " + prd);
    println(String + "quo= " + quo);
    println(String + "rem= " + rem);

    int sumL = 12 + 5;
    int difL = 12 - 5;
    int prdL = 12 * 5;
    int quoL = 12 / 5;
    int remL = 12 % 5;
    println(String + "sumL= " + sumL);
    println(String + "difL= " + difL);
    println(String + "prdL= " + prdL);
    println(String + "quoL= " + quoL);
    println(String + "remL= " + remL);

    int band = a & b;
    int bor  = a | b;
    int bxor = a ^ b;
    int shl  = a << 2;
    int shr  = a >> 2;
    println(String + "band= " + band);
    println(String + "bor= "  + bor);
    println(String + "bxor= " + bxor);
    println(String + "shl= "  + shl);
    println(String + "shr= "  + shr);

    int bandL = 12 & 5;
    int borL  = 12 | 5;
    int bxorL = 12 ^ 5;
    int shlL  = 12 << 2;
    int shrL  = 12 >> 2;
    println(String + "bandL= " + bandL);
    println(String + "borL= "  + borL);
    println(String + "bxorL= " + bxorL);
    println(String + "shlL= "  + shlL);
    println(String + "shrL= "  + shrL);

    int sh = 2;
    int shlV = a << sh;
    int shrV = a >> sh;
    println(String + "shlV= " + shlV);
    println(String + "shrV= " + shrV);

    int neg7 = -7;
    int neg8 = -8;
    int sShr = neg8 >> 1;
    int sDiv = neg7 / 2;
    int sRem = neg7 % 2;
    println(String + "sShr= " + sShr);
    println(String + "sDiv= " + sDiv);
    println(String + "sRem= " + sRem);

    bool t = true;
    bool f = false;

    bool aTT = t && t;
    bool aTF = t && f;
    bool aFT = f && t;
    bool aFF = f && f;
    println(String + "aTT= " + aTT);
    println(String + "aTF= " + aTF);
    println(String + "aFT= " + aFT);
    println(String + "aFF= " + aFF);

    bool oTT = t || t;
    bool oTF = t || f;
    bool oFT = f || t;
    bool oFF = f || f;
    println(String + "oTT= " + oTT);
    println(String + "oTF= " + oTF);
    println(String + "oFT= " + oFT);
    println(String + "oFF= " + oFF);

    bool xTT = t ^^ t;
    bool xTF = t ^^ f;
    bool xFT = f ^^ t;
    bool xFF = f ^^ f;
    println(String + "xTT= " + xTT);
    println(String + "xTF= " + xTF);
    println(String + "xFT= " + xFT);
    println(String + "xFF= " + xFF);

    bool aii = 5 && 3;
    bool aiz = 5 && 0;
    bool oiz = 0 || 7;
    bool xiz = 0 ^^ 7;
    println(String + "aii= " + aii);
    println(String + "aiz= " + aiz);
    println(String + "oiz= " + oiz);
    println(String + "xiz= " + xiz);

    int c = 12;

    bool eqAB = a == b;
    bool eqAC = a == c;
    bool neAB = a != b;
    bool neAC = a != c;
    bool ltAB = a < b;
    bool ltAC = a < c;
    bool leAB = a <= b;
    bool leAC = a <= c;
    bool gtAB = a > b;
    bool gtAC = a > c;
    bool geAB = a >= b;
    bool geAC = a >= c;
    println(String + "eqAB= " + eqAB);
    println(String + "eqAC= " + eqAC);
    println(String + "neAB= " + neAB);
    println(String + "neAC= " + neAC);
    println(String + "ltAB= " + ltAB);
    println(String + "ltAC= " + ltAC);
    println(String + "leAB= " + leAB);
    println(String + "leAC= " + leAC);
    println(String + "gtAB= " + gtAB);
    println(String + "gtAC= " + gtAC);
    println(String + "geAB= " + geAB);
    println(String + "geAC= " + geAC);

    bool eqL = 12 == 5;
    bool neL = 12 != 5;
    bool ltL = 12 < 5;
    bool leL = 12 <= 5;
    bool gtL = 12 > 5;
    bool geL = 12 >= 5;
    println(String + "eqL= " + eqL);
    println(String + "neL= " + neL);
    println(String + "ltL= " + ltL);
    println(String + "leL= " + leL);
    println(String + "gtL= " + gtL);
    println(String + "geL= " + geL);

    bool eqAL = a == 12;
    bool neAL = a != 12;
    bool ltAL = a < 12;
    bool leAL = a <= 12;
    bool gtAL = a > 12;
    bool geAL = a >= 12;
    println(String + "eqAL= " + eqAL);
    println(String + "neAL= " + neAL);
    println(String + "ltAL= " + ltAL);
    println(String + "leAL= " + leAL);
    println(String + "gtAL= " + gtAL);
    println(String + "geAL= " + geAL);

    int addEq = 100;  addEq += 5;    println(String + "addEq= " + addEq);
    int subEq = 100;  subEq -= 5;    println(String + "subEq= " + subEq);
    int mulEq = 100;  mulEq *= 3;    println(String + "mulEq= " + mulEq);
    int divEq = 100;  divEq /= 7;    println(String + "divEq= " + divEq);
    int modEq = 100;  modEq %= 7;    println(String + "modEq= " + modEq);

    int andEq = 12;   andEq &= 5;    println(String + "andEq= " + andEq);
    int orEq  = 12;   orEq  |= 5;    println(String + "orEq= "  + orEq);
    int xorEq = 12;   xorEq ^= 5;    println(String + "xorEq= " + xorEq);
    int shlEq = 12;   shlEq <<= 2;   println(String + "shlEq= " + shlEq);
    int shrEq = 12;   shrEq >>= 2;   println(String + "shrEq= " + shrEq);

    bool laEq = true;  laEq &&= 0;   println(String + "laEq= " + laEq);
    bool loEq = false; loEq ||= 7;   println(String + "loEq= " + loEq);
    bool lxEq = true;  lxEq ^^= 7;   println(String + "lxEq= " + lxEq);

    /* --- Type(value): a nameless temporary of a primitive type, bound by the DECL-INIT
       rules (fit-check, widen a narrower source, no truncation) --- */
    println(String + "ti8= "   + int8(-100));
    println(String + "ti16= "  + int16(30000));
    println(String + "ti32= "  + int32(5));
    println(String + "ti64= "  + int64(9000000000));
    println(String + "tu8= "   + uint8(255));
    println(String + "tu16= "  + uint16(65535));
    println(String + "tu32= "  + uint32(4000000000));
    println(String + "tu64= "  + uint64(9000000000000000000));
    println(String + "tint= "  + int(42));
    println(String + "tuint= " + uint(4000000000));
    println(String + "tiptr= " + intptr(-7));
    int8 twv = 42;
    println(String + "twiden= " + int64(twv));   // int8 source widens into int64

    /* --- Type(value) negatives: one //-block uncommented per run --- */

    //-EXPECT-ERROR: Integer literal does not fit in 'int32'.
    // int32 tnf = int32(0x8000_0000);
    // println(String + "tnf= " + tnf);

    //-EXPECT-ERROR: Cannot implicitly narrow 'int64' to 'int32'
    // int64 tnw = 5;
    // int32 tn = int32(tnw);
    // println(String + "tn= " + tn);

    //-EXPECT-ERROR: A primitive temporary 'Type(value)' requires exactly one value.
    // int32 tz = int32();
    // println(String + "tz= " + tz);

    //-EXPECT-ERROR: A primitive temporary 'Type(value)' takes exactly one value.
    // int32 tm = int32(1, 2);
    // println(String + "tm= " + tm);

    //-EXPECT-ERROR: A type is not an expression; write 'Type(value)' for a temporary.
    // int32 tb = int;
    // println(String + "tb= " + tb);

    //-EXPECT-ERROR: Expected variable name.
    // int32(5);

    return 0;
}
