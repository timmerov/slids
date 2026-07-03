/*
test implementation of expressions.

test precedence and parentheses.

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

Precedence / associativity / parentheses, int+bool only (which tree the parser
builds is type-independent; per-type leaf ops live in noninttype/mixedtype).

Two sections on purpose: the literal block validates the PARSE tree (it all
const-folds), the runtime block (variable operands) forces CODEGEN to build the
tree — const-folding masks codegen bugs. The runtime block found a real one:
emitUnary '!' returned its i1 without widening to dest_type, so `!v0 < v4`
emitted invalid IR. Fixed at codegen.cpp emitUnary (the '!' arm now
widen::convert's to dest_type, like the comparison/logical paths). If you add a
bool-producing expr feeding an int context here, that path is now exercised.

Grammar-grounded facts encoded by the tests: relational > equality > & ; == over
& (the C footgun); ^^ is co-equal with || and left-assoc, BELOW && (parseExpr
handles || and ^^ at one level). Ladder is complete in const + runtime form;
nothing open.
*/

int32 main() {

    // -- adjacent-pair precedence --
    int  p_mul_add  = 2 + 3 * 4;     __println("p_mul_add= "  + p_mul_add);   // 14 = 2 + (3*4)
    int  p_add_shl  = 1 + 2 << 3;    __println("p_add_shl= "  + p_add_shl);   // 24 = (1+2) << 3
    bool p_shl_lt   = 2 < 1 << 4;    __println("p_shl_lt= "   + p_shl_lt);    // true = 2 < (1<<4 = 16)
    int  p_and_xor  = 1 ^ 1 & 0;     __println("p_and_xor= "  + p_and_xor);   // 1 = 1 ^ (1&0 = 0)
    int  p_xor_or   = 1 | 0 ^ 1;     __println("p_xor_or= "   + p_xor_or);    // 1 = 1 | (0^1 = 1)
    bool p_or_and   = 0 && 1 | 1;    __println("p_or_and= "   + p_or_and);    // false = 0 && (1|1 = 1)
    bool p_and_or   = 1 || 0 && 0;   __println("p_and_or= "   + p_and_or);    // true = 1 || (0&&0 = 0)
    int  p_bnot_add = ~1 + 2;        __println("p_bnot_add= " + p_bnot_add);  // 0 = (~1 = -2) + 2
    bool p_lnot_lt  = !0 < 5;        __println("p_lnot_lt= "  + p_lnot_lt);   // true = (!0 = 1) < 5
    bool p_lt_eq    = 2 < 3 == 1;    __println("p_lt_eq= "    + p_lt_eq);     // true  = (2<3) == 1    relational over equality
    bool p_gt_ne    = 0 > 1 != 1;    __println("p_gt_ne= "    + p_gt_ne);     // true  = (0>1) != 1
    int  p_and_eq   = 2 & 0 == 0;    __println("p_and_eq= "   + p_and_eq);    // 0     = 2 & (0==0)    equality over &
    bool p_xor_and  = 1 ^^ 1 && 0;   __println("p_xor_and= "  + p_xor_and);   // true  = 1 ^^ (1&&0)   && over ^^
    bool p_or_xor   = 1 || 0 ^^ 1;   __println("p_or_xor= "   + p_or_xor);    // false = (1||0) ^^ 1   || and ^^ same level, left-assoc
    int  p_neg_add  = -2 + 3;        __println("p_neg_add= "  + p_neg_add);   // 1     = (-2) + 3      unary - over +
    int  p_sub_neg  = 2 - -3;        __println("p_sub_neg= "  + p_sub_neg);   // 5     = 2 - (-3)      binary - then unary -

    // -- parentheses override default precedence --
    int  pa_add_mul = (2 + 3) * 4;        __println("pa_add_mul= " + pa_add_mul);  // 20
    int  pa_neg_sum = -(2 + 3);           __println("pa_neg_sum= " + pa_neg_sum);  // -5
    int  pa_nested  = (1 + 2) * (3 + 4);  __println("pa_nested= "  + pa_nested);   // 21

    // -- left-associativity for same-level repeats --
    int  a_sub_sub  = 10 - 3 - 2;    __println("a_sub_sub= " + a_sub_sub);  // 5 = (10-3) - 2
    int  a_mul_div  = 2 * 3 / 4;     __println("a_mul_div= " + a_mul_div);  // 1 = (2*3)/4 int div
    int  a_shr_shr  = 16 >> 2 >> 1;  __println("a_shr_shr= " + a_shr_shr);  // 2 = (16>>2) >> 1
    bool a_lt_lt    = 1 < 2 < 3;     __println("a_lt_lt= "   + a_lt_lt);    // true = (1<2) < 3 = 1<3

    // -- runtime evaluation: variable operands defeat const-folding, so codegen
    //    (not constfold) must build each tree in precedence order --
    int v0 = 0; int v1 = 1; int v2 = 2; int v3 = 3; int v4 = 4; int v10 = 10; int v16 = 16;

    int  rt_mul_add  = v2 + v3 * v4;        __println("rt_mul_add= "  + rt_mul_add);   // 14
    int  rt_add_shl  = v1 + v2 << v3;       __println("rt_add_shl= "  + rt_add_shl);   // 24
    bool rt_shl_lt   = v2 < v1 << v4;       __println("rt_shl_lt= "   + rt_shl_lt);    // true
    int  rt_and_xor  = v1 ^ v1 & v0;        __println("rt_and_xor= "  + rt_and_xor);   // 1
    int  rt_xor_or   = v1 | v0 ^ v1;        __println("rt_xor_or= "   + rt_xor_or);    // 1
    bool rt_or_and   = v0 && v1 | v1;       __println("rt_or_and= "   + rt_or_and);    // false
    bool rt_and_or   = v1 || v0 && v0;      __println("rt_and_or= "   + rt_and_or);    // true
    bool rt_lt_eq    = v2 < v3 == v1;       __println("rt_lt_eq= "    + rt_lt_eq);     // true
    int  rt_and_eq   = v2 & v0 == v0;       __println("rt_and_eq= "   + rt_and_eq);    // 0
    bool rt_xor_and  = v1 ^^ v1 && v0;      __println("rt_xor_and= "  + rt_xor_and);   // true
    bool rt_or_xor   = v1 || v0 ^^ v1;      __println("rt_or_xor= "   + rt_or_xor);    // false
    int  rt_bnot_add = ~v1 + v2;            __println("rt_bnot_add= " + rt_bnot_add);  // 0
    bool rt_lnot_lt  = !v0 < v4;            __println("rt_lnot_lt= "  + rt_lnot_lt);   // true
    int  rt_neg_add  = -v2 + v3;            __println("rt_neg_add= "  + rt_neg_add);   // 1
    int  rt_sub_neg  = v2 - -v3;            __println("rt_sub_neg= "  + rt_sub_neg);   // 5
    int  rt_add_mul  = (v2 + v3) * v4;      __println("rt_add_mul= "  + rt_add_mul);   // 20
    int  rt_nested   = (v1 + v2) * (v3 + v4); __println("rt_nested= " + rt_nested);    // 21
    int  rt_sub_sub  = v10 - v3 - v2;       __println("rt_sub_sub= "  + rt_sub_sub);   // 5
    int  rt_mul_div  = v2 * v3 / v4;        __println("rt_mul_div= "  + rt_mul_div);   // 1
    int  rt_shr_shr  = v16 >> v2 >> v1;     __println("rt_shr_shr= "  + rt_shr_shr);   // 2
    bool rt_lt_lt    = v1 < v2 < v3;        __println("rt_lt_lt= "    + rt_lt_lt);     // true

    // -- repeated unary + equality associativity (runtime) --
    bool rt_lnot2    = !!v1;                 __println("rt_lnot2= "    + rt_lnot2);     // true = !(!1)
    int  rt_bnot2    = ~~v1;                 __println("rt_bnot2= "    + rt_bnot2);     // 1    = ~(~1)
    int  rt_neg2     = - -v2;                __println("rt_neg2= "     + rt_neg2);      // 2    = -(-2)
    bool rt_eq_assoc = v2 == v2 == v1;       __println("rt_eq_assoc= " + rt_eq_assoc); // true = (v2==v2)==v1  (right-assoc -> false)

    return 0;
}
