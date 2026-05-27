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

    // -- parentheses override default precedence --
    int  pa_add_mul = (2 + 3) * 4;        __println("pa_add_mul= " + pa_add_mul);  // 20
    int  pa_neg_sum = -(2 + 3);           __println("pa_neg_sum= " + pa_neg_sum);  // -5
    int  pa_nested  = (1 + 2) * (3 + 4);  __println("pa_nested= "  + pa_nested);   // 21

    // -- left-associativity for same-level repeats --
    int  a_sub_sub  = 10 - 3 - 2;    __println("a_sub_sub= " + a_sub_sub);  // 5 = (10-3) - 2
    int  a_mul_div  = 2 * 3 / 4;     __println("a_mul_div= " + a_mul_div);  // 1 = (2*3)/4 int div
    int  a_shr_shr  = 16 >> 2 >> 1;  __println("a_shr_shr= " + a_shr_shr);  // 2 = (16>>2) >> 1
    bool a_lt_lt    = 1 < 2 < 3;     __println("a_lt_lt= "   + a_lt_lt);    // true = (1<2) < 3 = 1<3

    return 0;
}
