/*
test implementation of expressions.

ensure we parse mixed type expressions correctly.
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

    // -- arith / common-type cascade --
    int64   r_i8_i64  = bi8  + bi64;  __println("r_i8_i64= "  + r_i8_i64);   // same family widen
    int32   r_u8_i32  = bu8  + bi32;  __println("r_u8_i32= "  + r_u8_i32);   // narrow unsigned + wider signed
    int64   r_i32_u32 = bi32 + bu32;  __println("r_i32_u32= " + r_i32_u32);  // mixed sign equal width
    int64   r_u32_i8  = bu32 + bi8;   __println("r_u32_i8= "  + r_u32_i8);   // wider unsigned + narrower signed
    float32 r_i16_f32 = bi16 + bf32;  __println("r_i16_f32= " + r_i16_f32);  // int fits float
    float64 r_i32_f32 = bi32 + bf32;  __println("r_i32_f32= " + r_i32_f32);  // int bumps to float64

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

    // -- literal flex --
    int8    r_i8_lit      = bi8  + 5;     __println("r_i8_lit= "      + r_i8_lit);       // literal fits typed
    int     r_u8_big      = bu8  + 1000;  __println("r_u8_big= "      + r_u8_big);       // literal doesn't fit → default + common
    int8    r_ll_fold     = 1 + 2;        __println("r_ll_fold= "     + r_ll_fold);      // literal+literal fold
    float32 r_f32_int_lit = bf32 + 3;     __println("r_f32_int_lit= " + r_f32_int_lit);  // int literal flexes to float
    int8    r_f_lit_int   = 3.0;          __println("r_f_lit_int= "   + r_f_lit_int);    // float literal → int (integer-valued)

    // -- bool + char edges --
    int32 r_bool_i32 = bb + bi32;      __println("r_bool_i32= " + r_bool_i32);  // bool widens cleanly
    int16 r_char_i8  = bc & bi8;       __println("r_char_i8= "  + r_char_i8);   // char (uint8) vs int8 mixed sign

    // -- negative: no common type --
    //-EXPECT-ERROR: No common type for 'uint64' and 'int8'; use an explicit type conversion.
    // int64 bad_nc = bu64 + bi8;

    // -- negative: common type widens to float64, assign narrows --
    //-EXPECT-ERROR: Cannot implicitly narrow 'float64' to 'float32'; use an explicit type conversion.
    // float32 bad_nar = bi32 + bf32;

    // -- negative: confusing-error (int32+uint32 → int64, assign narrows back to int32) --
    //-EXPECT-ERROR: Cannot implicitly narrow 'int64' to 'int32'; use an explicit type conversion.
    // int32 bad_conf = bi32 + bu32;

    // -- negative: comparison literal vs uint64 (literal -1 → int32, no common with uint64) --
    //-EXPECT-ERROR: No common type for 'uint64' and 'int32'; use an explicit type conversion.
    // bool bad_cmp = bu64 == -1;

    return 0;
}
