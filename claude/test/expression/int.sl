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

int32 main() {

    int u = 5;
    int pu = +u;
    int nu = -u;
    int cu = ~u;
    bool bu = !u;
    __println("pu= " + pu);
    __println("nu= " + nu);
    __println("cu= " + cu);
    __println("bu= " + bu);

    int p5 = +5;
    int n5 = -5;
    int c5 = ~5;
    bool b5 = !5;
    __println("p5= " + p5);
    __println("n5= " + n5);
    __println("c5= " + c5);
    __println("b5= " + b5);

    int a = 12;
    int b = 5;
    int sum = a + b;
    int dif = a - b;
    int prd = a * b;
    int quo = a / b;
    int rem = a % b;
    __println("sum= " + sum);
    __println("dif= " + dif);
    __println("prd= " + prd);
    __println("quo= " + quo);
    __println("rem= " + rem);

    int sumL = 12 + 5;
    int difL = 12 - 5;
    int prdL = 12 * 5;
    int quoL = 12 / 5;
    int remL = 12 % 5;
    __println("sumL= " + sumL);
    __println("difL= " + difL);
    __println("prdL= " + prdL);
    __println("quoL= " + quoL);
    __println("remL= " + remL);

    int band = a & b;
    int bor  = a | b;
    int bxor = a ^ b;
    int shl  = a << 2;
    int shr  = a >> 2;
    __println("band= " + band);
    __println("bor= "  + bor);
    __println("bxor= " + bxor);
    __println("shl= "  + shl);
    __println("shr= "  + shr);

    int bandL = 12 & 5;
    int borL  = 12 | 5;
    int bxorL = 12 ^ 5;
    int shlL  = 12 << 2;
    int shrL  = 12 >> 2;
    __println("bandL= " + bandL);
    __println("borL= "  + borL);
    __println("bxorL= " + bxorL);
    __println("shlL= "  + shlL);
    __println("shrL= "  + shrL);

    int sh = 2;
    int shlV = a << sh;
    int shrV = a >> sh;
    __println("shlV= " + shlV);
    __println("shrV= " + shrV);

    int neg7 = -7;
    int neg8 = -8;
    int sShr = neg8 >> 1;
    int sDiv = neg7 / 2;
    int sRem = neg7 % 2;
    __println("sShr= " + sShr);
    __println("sDiv= " + sDiv);
    __println("sRem= " + sRem);

    bool t = true;
    bool f = false;

    bool aTT = t && t;
    bool aTF = t && f;
    bool aFT = f && t;
    bool aFF = f && f;
    __println("aTT= " + aTT);
    __println("aTF= " + aTF);
    __println("aFT= " + aFT);
    __println("aFF= " + aFF);

    bool oTT = t || t;
    bool oTF = t || f;
    bool oFT = f || t;
    bool oFF = f || f;
    __println("oTT= " + oTT);
    __println("oTF= " + oTF);
    __println("oFT= " + oFT);
    __println("oFF= " + oFF);

    bool xTT = t ^^ t;
    bool xTF = t ^^ f;
    bool xFT = f ^^ t;
    bool xFF = f ^^ f;
    __println("xTT= " + xTT);
    __println("xTF= " + xTF);
    __println("xFT= " + xFT);
    __println("xFF= " + xFF);

    bool aii = 5 && 3;
    bool aiz = 5 && 0;
    bool oiz = 0 || 7;
    bool xiz = 0 ^^ 7;
    __println("aii= " + aii);
    __println("aiz= " + aiz);
    __println("oiz= " + oiz);
    __println("xiz= " + xiz);

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
    __println("eqAB= " + eqAB);
    __println("eqAC= " + eqAC);
    __println("neAB= " + neAB);
    __println("neAC= " + neAC);
    __println("ltAB= " + ltAB);
    __println("ltAC= " + ltAC);
    __println("leAB= " + leAB);
    __println("leAC= " + leAC);
    __println("gtAB= " + gtAB);
    __println("gtAC= " + gtAC);
    __println("geAB= " + geAB);
    __println("geAC= " + geAC);

    bool eqL = 12 == 5;
    bool neL = 12 != 5;
    bool ltL = 12 < 5;
    bool leL = 12 <= 5;
    bool gtL = 12 > 5;
    bool geL = 12 >= 5;
    __println("eqL= " + eqL);
    __println("neL= " + neL);
    __println("ltL= " + ltL);
    __println("leL= " + leL);
    __println("gtL= " + gtL);
    __println("geL= " + geL);

    bool eqAL = a == 12;
    bool neAL = a != 12;
    bool ltAL = a < 12;
    bool leAL = a <= 12;
    bool gtAL = a > 12;
    bool geAL = a >= 12;
    __println("eqAL= " + eqAL);
    __println("neAL= " + neAL);
    __println("ltAL= " + ltAL);
    __println("leAL= " + leAL);
    __println("gtAL= " + gtAL);
    __println("geAL= " + geAL);

    int addEq = 100;  addEq += 5;    __println("addEq= " + addEq);
    int subEq = 100;  subEq -= 5;    __println("subEq= " + subEq);
    int mulEq = 100;  mulEq *= 3;    __println("mulEq= " + mulEq);
    int divEq = 100;  divEq /= 7;    __println("divEq= " + divEq);
    int modEq = 100;  modEq %= 7;    __println("modEq= " + modEq);

    int andEq = 12;   andEq &= 5;    __println("andEq= " + andEq);
    int orEq  = 12;   orEq  |= 5;    __println("orEq= "  + orEq);
    int xorEq = 12;   xorEq ^= 5;    __println("xorEq= " + xorEq);
    int shlEq = 12;   shlEq <<= 2;   __println("shlEq= " + shlEq);
    int shrEq = 12;   shrEq >>= 2;   __println("shrEq= " + shrEq);

    bool laEq = true;  laEq &&= 0;   __println("laEq= " + laEq);
    bool loEq = false; loEq ||= 7;   __println("loEq= " + loEq);
    bool lxEq = true;  lxEq ^^= 7;   __println("lxEq= " + lxEq);

    /* --- Type(value): a nameless temporary of a primitive type, bound by the DECL-INIT
       rules (fit-check, widen a narrower source, no truncation) --- */
    __println("ti8= "   + int8(-100));
    __println("ti16= "  + int16(30000));
    __println("ti32= "  + int32(5));
    __println("ti64= "  + int64(9000000000));
    __println("tu8= "   + uint8(255));
    __println("tu16= "  + uint16(65535));
    __println("tu32= "  + uint32(4000000000));
    __println("tu64= "  + uint64(9000000000000000000));
    __println("tint= "  + int(42));
    __println("tuint= " + uint(4000000000));
    __println("tiptr= " + intptr(-7));
    int8 twv = 42;
    __println("twiden= " + int64(twv));   // int8 source widens into int64

    /* --- Type(value) negatives: one //-block uncommented per run --- */

    //-EXPECT-ERROR: Integer literal does not fit in 'int32'.
    // int32 tnf = int32(0x8000_0000);
    // __println("tnf= " + tnf);

    //-EXPECT-ERROR: Cannot implicitly narrow 'int64' to 'int32'
    // int64 tnw = 5;
    // int32 tn = int32(tnw);
    // __println("tn= " + tn);

    //-EXPECT-ERROR: A primitive temporary 'Type(value)' requires exactly one value.
    // int32 tz = int32();
    // __println("tz= " + tz);

    //-EXPECT-ERROR: A primitive temporary 'Type(value)' takes exactly one value.
    // int32 tm = int32(1, 2);
    // __println("tm= " + tm);

    //-EXPECT-ERROR: A type is not an expression; write 'Type(value)' for a temporary.
    // int32 tb = int;
    // __println("tb= " + tb);

    //-EXPECT-ERROR: Expected variable name.
    // int32(5);

    return 0;
}
