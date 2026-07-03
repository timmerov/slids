/*
test implementation of expressions.

test mixed type expressions.
this coordinates with the widen rules.

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

This file owns the widen / common-type rules: each operand extends to the
common type with its OWN signedness (unsigned -> zext, signed -> sext), int and
float never silently mix, literals flex to a target by value.

Coverage added this pass: high-bit unsigned mixed with a wider signed type
(hu8/hu16/hu32) so zext-vs-sext is output-locked (a sext bug flips 300->44,
true->false — was invisible before, since every operand had been positive);
mixed-type logical (each operand -> bool independently, covers the 0/0.0 falsy
rule); literal-fit positives + negatives; and bool-valued sub-expressions
widening (zext) into arithmetic/bitwise (r_lt_add..r_not_xor).

Open: a char >= 128 can't be written as a source literal, so char's
sign-extension into a mixed expression stays IR-verified only, not output-locked.
*/

int32 main() {

    // -- setup --
    int8    bi8  = 4;     __println("bi8= "  + bi8);
    int16   bi16 = 10;    __println("bi16= " + bi16);
    int32   bi32 = 100;   __println("bi32= " + bi32);
    int64   bi64 = 1000;  __println("bi64= " + bi64);
    uint8   bu8  = 4;     __println("bu8= "  + bu8);
    uint16  bu16 = 10;    __println("bu16= " + bu16);
    uint32  bu32 = 100;   __println("bu32= " + bu32);
    uint64  bu64 = 1000;  __println("bu64= " + bu64);
    char    bc   = 'A';   __println("bc= "   + bc);
    bool    bb   = true;  __println("bb= "   + bb);
    float32 bf32 = 1.5;   __println("bf32= " + bf32);
    float64 bf64 = 2.5;   __println("bf64= " + bf64);
    int32   shc  = 2;     __println("shc= "  + shc);
    uint8   hu8  = 200;        __println("hu8= "  + hu8);
    uint16  hu16 = 40000;      __println("hu16= " + hu16);
    uint32  hu32 = 2147483648; __println("hu32= " + hu32);
    float32 zf32 = 0.0;        __println("zf32= " + zf32);
    int8    zi8  = 0;          __println("zi8= "  + zi8);

    // -- arith / common-type cascade --
    int64   r_i8_i64  = bi8  + bi64;  __println("r_i8_i64= "  + r_i8_i64);   // same family widen
    int32   r_u8_i32  = bu8  + bi32;  __println("r_u8_i32= "  + r_u8_i32);   // narrow unsigned + wider signed
    int64   r_i32_u32 = bi32 + bu32;  __println("r_i32_u32= " + r_i32_u32);  // mixed sign equal width
    int64   r_u32_i8  = bu32 + bi8;   __println("r_u32_i8= "  + r_u32_i8);   // wider unsigned + narrower signed

    // -- mixed-sign, high-bit unsigned: the unsigned operand must zero-extend, not sign-extend --
    int32 r_hu8_i32  = hu8  + bi32;    __println("r_hu8_i32= "  + r_hu8_i32);     // 300   (sext bug -> 44)
    int32 r_hu16_i32 = hu16 + bi32;    __println("r_hu16_i32= " + r_hu16_i32);    // 40100 (sext bug -> -25436)
    bool  cmp_hu32_i32 = hu32 > bi32;  __println("cmp_hu32_i32= " + cmp_hu32_i32); // true  (sext bug -> false)

    // -- bitwise (int-only common-type) --
    int32 r_u16_or_i16 = bu16 | bi16;  __println("r_u16_or_i16= " + r_u16_or_i16);

    // -- shift (LHS dictates result; RHS any int) --
    int8   r_shl_i8  = bi8  << shc;    __println("r_shl_i8= "  + r_shl_i8);
    uint16 r_shr_u16 = bu16 >> 1;      __println("r_shr_u16= " + r_shr_u16);

    // -- comparison (common-type widen → bool) --
    bool cmp_u32_i32 = bu32 == bi32;   __println("cmp_u32_i32= " + cmp_u32_i32);
    bool cmp_f32_f64 = bf32 != bf64;   __println("cmp_f32_f64= " + cmp_f32_f64);

    // -- logical (each operand → bool independently) --
    bool log_and = bi32 && bf64;       __println("log_and= " + log_and);
    // -- logical across mixed types (no common type; covers the 0 / 0.0 falsy rule) --
    bool log_if  = bi8  && bf32;   __println("log_if= "  + log_if);    // T && T = true
    bool log_uc  = bu16 ^^ bc;     __println("log_uc= "  + log_uc);    // T ^^ T = false
    bool log_bf  = bb   || zf32;   __println("log_bf= "  + log_bf);    // T || F = true
    bool log_cf  = zi8  && bf64;   __println("log_cf= "  + log_cf);    // F && T = false
    bool log_fz  = bf32 || zf32;   __println("log_fz= "  + log_fz);    // T || F = true
    bool log_zx  = zf32 ^^ zi8;    __println("log_zx= "  + log_zx);    // F ^^ F = false
    bool log_nzf = !zf32;          __println("log_nzf= " + log_nzf);   // !(0.0) = true

    // -- literal flex --
    int8    r_i8_lit      = bi8  + 5;     __println("r_i8_lit= "      + r_i8_lit);       // literal fits typed
    int     r_u8_big      = bu8  + 1000;  __println("r_u8_big= "      + r_u8_big);       // literal doesn't fit → default + common
    int8    r_ll_fold     = 1 + 2;        __println("r_ll_fold= "     + r_ll_fold);      // literal+literal fold

    // -- literal fit: a literal flexes to its target by VALUE, even past the signed default --
    uint8  fit_u8  = 255;          __println("fit_u8= "  + fit_u8);
    int8   fit_i8  = -128;         __println("fit_i8= "  + fit_i8);
    uint16 fit_u16 = 65535;        __println("fit_u16= " + fit_u16);
    uint32 fit_u32 = 4294967295;   __println("fit_u32= " + fit_u32);

    // -- bool + char edges --
    int32 r_bool_i32 = bb + bi32;      __println("r_bool_i32= " + r_bool_i32);  // bool widens cleanly
    int16 r_char_i8  = bc & bi8;       __println("r_char_i8= "  + r_char_i8);   // char (uint8) vs int8 mixed sign

    // -- bool-valued sub-expression widens (zext) into arithmetic / bitwise --
    int32 r_lt_add  = (bi8 < bi32) + bi32;   __println("r_lt_add= "  + r_lt_add);   // 101 = 1 + 100   (comparison result)
    int32 r_not_mul = !zi8 * bi32;           __println("r_not_mul= " + r_not_mul);  // 100 = 1 * 100   (logical-not result)
    int32 r_and_sub = bi32 - (bb && bi32);   __println("r_and_sub= " + r_and_sub);  // 99  = 100 - 1   (logical && result)
    int32 r_lt_or   = (bi8 < bi32) | bi32;   __println("r_lt_or= "   + r_lt_or);    // 101 = 1 | 100
    int32 r_not_xor = !zi8 ^ bi8;            __println("r_not_xor= " + r_not_xor);  // 5   = 1 ^ 4

    // -- negative: no common type --
    // (each negative reads its local so the type error surfaces ahead of the
    //  unused-local check, which would otherwise mask it.)
    //-EXPECT-ERROR: No common type for 'uint64' and 'int8'; use an explicit type conversion.
    // int64 bad_nc = bu64 + bi8;
    // __println("bad_nc= " + bad_nc);

    // -- negative: int and float never silently mix --
    //-EXPECT-ERROR: No common type for 'int16' and 'float32'; use an explicit type conversion.
    // float32 bad_i16_f32 = bi16 + bf32;
    // __println("bad_i16_f32= " + bad_i16_f32);
    //-EXPECT-ERROR: No common type for 'int32' and 'float32'; use an explicit type conversion.
    // float64 bad_i32_f32 = bi32 + bf32;
    // __println("bad_i32_f32= " + bad_i32_f32);
    //-EXPECT-ERROR: No common type for 'float32' and 'int'; use an explicit type conversion.
    // float32 bad_f32_intlit = bf32 + 3;
    // __println("bad_f32_intlit= " + bad_f32_intlit);
    //-EXPECT-ERROR: Cannot implicitly convert 'float' to 'int8'; use an explicit type conversion.
    // int8 bad_fltlit_int = 3.0;
    // __println("bad_fltlit_int= " + bad_fltlit_int);

    // -- negative: confusing-error (int32+uint32 → int64, assign narrows back to int32) --
    //-EXPECT-ERROR: Cannot implicitly narrow 'int64' to 'int32'; use an explicit type conversion.
    // int32 bad_conf = bi32 + bu32;
    // __println("bad_conf= " + bad_conf);

    // -- negative: comparison literal vs uint64 (literal -1 → int32, no common with uint64) --
    //-EXPECT-ERROR: No common type for 'uint64' and 'int'; use an explicit type conversion.
    // bool bad_cmp = bu64 == -1;
    // __println("bad_cmp= " + bad_cmp);

    // -- negative: literal doesn't fit its target (over max, under min, negative-into-unsigned) --
    //-EXPECT-ERROR: Integer literal does not fit in 'uint8'.
    // uint8 over_u8 = 256;
    // __println("over_u8= " + over_u8);
    //-EXPECT-ERROR: Integer literal does not fit in 'int8'.
    // int8 over_i8 = 128;
    // __println("over_i8= " + over_i8);
    //-EXPECT-ERROR: Integer literal does not fit in 'uint8'.
    // uint8 neg_u8 = -1;
    // __println("neg_u8= " + neg_u8);

    return 0;
}
