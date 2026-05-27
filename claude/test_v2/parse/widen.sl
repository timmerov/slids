/*
test the built-in primitives.

number widening rules when the target type is known:
these rules apply everywhere widening can happen.
including but not limited to: expressions, literals, parameters, variables, etc.

signed integers are silently widened to larger signed integers.
unsigned integers are silently widened to larger unsigned integers.
unsigned integers are silently widened to a larger unsigned integer,
then silently reinterpreted as a signed integer of the same larger size.
floating point numbers are silently widened to larger floating point numbers.
integer types may be silently converted to floating point if the entire range fits.
int16 -> float32, int32 -> float64.

losing information without an explicit type conversion is a compile error:
truncating to a smaller number type - integer and floating point.
converting signed integers to unsigned.
converting 32 or 64 bit integers to float32.
converting 64 bit integers to float64.

decimal integer literals are flexible type.
their default type is int,
unless the value is too big,
then the type is int64,
unless the value is too big,
then the type is uint64.

hex and binary integer literals are flexible type.
their default type is uint,
unless the value is too big,
then the type is uint64,

floating point literals are flexible type.
their default type is float.
unless the value is too big.
then the type is float64.
floating point literals are silently rounded to match the target type precision.
3.14 becomes 3.1400001049 for target float32.

integer literals may be silently type converted to floating point if they fit.
floating point literals may be silently type converted to integer types if the
value is an integer and they fit.

bool is a 1 bit unsigned integer.
true and false are number literals with values 1 and 0.

char, intptr, float, are types that depends on the compiler and/or the operating system.
char is typically uint8.
intptr is typically int64.
float is typically float32.
maybe these are configurable in the compiler.
maybe not.
the author should not make assumptions.
bool, char, intptr, float follow the rules that apply to their implementation type.

some binary operations have special widening rules:
shift operations << >> do not need the rhs to be widened - all types are accepted.
the result of the shift operation is the type of the lhs.
logical operations && || ^^ consume condition-expression - all types are accepted, no widening needed.
logical operations (generally) produce bool - no explicit conversion needed.

the next sections apply to the remaining ordinary binary opertions: + - * / % & | ^ == != >= <= > <

binary widening rules for two typed numbers:
the result type is the smallest type large enough to hold either operand.
the operation is then applied using the result type.
uint32 + int32 -> int64 + int64 -> int64
it is a compile error if no such type exists.

binary operations between typed number and literal number:
if a number literal fits into the type, then the result is the type.
otherwise the number literal assumes its default type- int32, int64, uint64;
and normal binary widening rules are applied.

binary operations (includes the special cases) between two literal numbers:
if the numbers need to be widened in order to perform the operation,
then each literal assumes its default type.
and normal widening rules (including special cases) apply.
do the operation to create new untyped literal number.

note: this may produce a confusing error message:
    int32 = int32 + uint32;
rhs is widened to int64 which can't be silently truncated to int32.
*/

int32 main() {

    bool xb = true;       __println("xb= " + xb);
    char xc = 'A';        __println("xc= " + xc);
    int xi = 1;           __println("xi= " + xi);
    uint xu = 2;          __println("xu= " + xu);
    intptr xip = 3;       __println("xip= " + xip);
    int8 xi8 = 4;         __println("xi8= " + xi8);
    int16 xi16 = 5;       __println("xi16= " + xi16);
    int32 xi32 = 6;       __println("xi32= " + xi32);
    int64 xi64 = 7;       __println("xi64= " + xi64);
    uint8 xu8 = 8;        __println("xu8= " + xu8);
    uint16 xu16 = 9;      __println("xu16= " + xu16);
    uint32 xu32 = 10;     __println("xu32= " + xu32);
    uint64 xu64 = 11;     __println("xu64= " + xu64);
    float xf = 12.0;      __println("xf= " + xf);
    float32 xf32 = 13.0;  __println("xf32= " + xf32);
    float64 xf64 = 14.0;  __println("xf64= " + xf64);

    xb = 0;  __println("xb= " + xb);
    xb = 1;  __println("xb= " + xb);
    //-EXPECT-ERROR: Integer literal does not fit in 'bool'.
    // xb = -1;
    //-EXPECT-ERROR: Integer literal does not fit in 'bool'.
    // xb = 2;

    xi8 = 127;   __println("xi8= " + xi8);
    xi8 = -128;  __println("xi8= " + xi8);
    //-EXPECT-ERROR: Integer literal does not fit in 'int8'.
    // xi8 = 128;
    //-EXPECT-ERROR: Integer literal does not fit in 'int8'.
    // xi8 = -129;

    xi16 = 32_767;   __println("xi16= " + xi16);
    xi16 = -32_768;  __println("xi16= " + xi16);
    //-EXPECT-ERROR: Integer literal does not fit in 'int16'.
    // xi16 = 32_768;
    //-EXPECT-ERROR: Integer literal does not fit in 'int16'.
    // xi16 = -32_769;

    xi32 = 2_147_483_647;   __println("xi32= " + xi32);
    xi32 = -2_147_483_648;  __println("xi32= " + xi32);
    //-EXPECT-ERROR: Integer literal does not fit in 'int32'.
    // xi32 = 2_147_483_648;
    //-EXPECT-ERROR: Integer literal does not fit in 'int32'.
    // xi32 = -2_147_483_649;

    xi64 = 9_223_372_036_854_775_807;   __println("xi64= " + xi64);
    xi64 = -9_223_372_036_854_775_808;  __println("xi64= " + xi64);
    //-EXPECT-ERROR: Integer literal does not fit in 'int64'.
    // xi64 = 9_223_372_036_854_775_808;
    //-EXPECT-ERROR: Integer literal does not fit in 'int64'.
    // xi64 = -9_223_372_036_854_775_809;

    xu8 = 0;    __println("xu8= " + xu8);
    xu8 = 255;  __println("xu8= " + xu8);
    //-EXPECT-ERROR: Integer literal does not fit in 'uint8'.
    // xu8 = -1;
    //-EXPECT-ERROR: Integer literal does not fit in 'uint8'.
    // xu8 = 256;

    xu16 = 0;       __println("xu16= " + xu16);
    xu16 = 65_535;  __println("xu16= " + xu16);
    //-EXPECT-ERROR: Integer literal does not fit in 'uint16'.
    // xu16 = -1;
    //-EXPECT-ERROR: Integer literal does not fit in 'uint16'.
    // xu16 = 65_536;

    xu32 = 0;              __println("xu32= " + xu32);
    xu32 = 4_294_967_295;  __println("xu32= " + xu32);
    //-EXPECT-ERROR: Integer literal does not fit in 'uint32'.
    // xu32 = -1;
    //-EXPECT-ERROR: Integer literal does not fit in 'uint32'.
    // xu32 = 4_294_967_296;

    xu64 = 0;                           __println("xu64= " + xu64);
    xu64 = 18_446_744_073_709_551_615;  __println("xu64= " + xu64);
    //-EXPECT-ERROR: Integer literal does not fit in 'uint64'.
    // xu64 = -1;
    //-EXPECT-ERROR: Integer literal overflows uint64.
    // xu64 = 18_446_744_073_709_551_616;

    xu = 0;              __println("xu= " + xu);
    xu = 4_294_967_295;  __println("xu= " + xu);
    //-EXPECT-ERROR: Integer literal does not fit in 'uint'.
    // xu = -1;
    //-EXPECT-ERROR: Integer literal does not fit in 'uint'.
    // xu = 4_294_967_296;

    xf32 = 1.0;   __println("xf32= " + xf32);
    xf32 = -1.0;  __println("xf32= " + xf32);
    //-EXPECT-ERROR: Float literal does not fit in 'float32'.
    // xf32 = 1e40;
    //-EXPECT-ERROR: Float literal does not fit in 'float32'.
    // xf32 = -1e40;

    // ===== mixed-type binary expressions =====

    // -- setup --
    int8    bi8  = 4;              __println("bi8= "  + bi8);
    int16   bi16 = 300;            __println("bi16= " + bi16);
    int32   bi32 = 100_000;        __println("bi32= " + bi32);
    int64   bi64 = 5_000_000_000;  __println("bi64= " + bi64);
    uint8   bu8  = 200;            __println("bu8= "  + bu8);
    uint16  bu16 = 50_000;         __println("bu16= " + bu16);
    uint32  bu32 = 4_000_000_000;  __println("bu32= " + bu32);
    float32 bf32 = 1.5;            __println("bf32= " + bf32);
    float64 bf64 = 2.5;            __println("bf64= " + bf64);

    // -- same family, different width (narrow widens to wider) --
    int16   r_i8_i16   = bi8  + bi16;  __println("r_i8_i16= "   + r_i8_i16);
    int32   r_i16_i32  = bi16 + bi32;  __println("r_i16_i32= "  + r_i16_i32);
    int64   r_i32_i64  = bi32 + bi64;  __println("r_i32_i64= "  + r_i32_i64);
    uint32  r_u8_u32   = bu8  + bu32;  __println("r_u8_u32= "   + r_u8_u32);
    float64 r_f32_f64  = bf32 + bf64;  __println("r_f32_f64= "  + r_f32_f64);

    // -- mixed sign, unsigned narrow + signed wider --
    int16 r_u8_i16  = bu8  + bi16;  __println("r_u8_i16= "  + r_u8_i16);
    int32 r_u16_i32 = bu16 + bi32;  __println("r_u16_i32= " + r_u16_i32);
    int64 r_u32_i64 = bu32 + bi64;  __println("r_u32_i64= " + r_u32_i64);

    // -- mixed sign, same width (both widen to next-bigger signed) --
    int16 r_u8_i8   = bu8  + bi8;   __println("r_u8_i8= "   + r_u8_i8);
    int32 r_u16_i16 = bu16 + bi16;  __println("r_u16_i16= " + r_u16_i16);
    int64 r_u32_i32 = bu32 + bi32;  __println("r_u32_i32= " + r_u32_i32);

    // -- mixed sign, unsigned wider + signed narrow --
    int64 r_u32_i8  = bu32 + bi8;   __println("r_u32_i8= "  + r_u32_i8);
    int64 r_u32_i16 = bu32 + bi16;  __println("r_u32_i16= " + r_u32_i16);

    // -- int + float (value-preserving); bool is 1-bit unsigned, fits any float --
    float32 r_i16_f32  = bi16 + bf32;  __println("r_i16_f32= "  + r_i16_f32);
    float32 r_u8_f32   = bu8  + bf32;  __println("r_u8_f32= "   + r_u8_f32);
    float64 r_i32_f64  = bi32 + bf64;  __println("r_i32_f64= "  + r_i32_f64);
    float64 r_u16_f64  = bu16 + bf64;  __println("r_u16_f64= "  + r_u16_f64);
    float32 r_bool_f32 = xb   + bf32;  __println("r_bool_f32= " + r_bool_f32);  // bool → float32 via uitofp

    // -- comparison on mixed types - result is bool --
    bool cmp_i8_i32  = bi8  < bi32;    __println("cmp_i8_i32= "  + cmp_i8_i32);
    bool cmp_u32_i32 = bu32 == bi32;   __println("cmp_u32_i32= " + cmp_u32_i32);
    bool cmp_i32_f64 = bi32 != bf64;   __println("cmp_i32_f64= " + cmp_i32_f64);

    // -- shift - lhs dictates result type; rhs any int --
    int8  r_shl_i8  = bi8  << bi32;  __println("r_shl_i8= "  + r_shl_i8);
    int64 r_shr_i64 = bi64 >> bi8;   __println("r_shr_i64= " + r_shr_i64);
    int8  r_shl_lit = bi8  << 3;     __println("r_shl_lit= " + r_shl_lit);

    // -- logical - all builtins coerce to bool independently --
    bool log_and = bi32 && bf64;     __println("log_and= " + log_and);
    bool log_or  = bu32 || bi8;      __println("log_or= "  + log_or);
    bool log_xor = bf32 ^^ bi64;     __println("log_xor= " + log_xor);

    // -- typed + literal - literal fits into type --
    int8    r_i8_lit  = bi8  + 5;    __println("r_i8_lit= "  + r_i8_lit);
    float32 r_f32_int = bf32 + 3;    __println("r_f32_int= " + r_f32_int);
    float32 r_f32_flt = bf32 + 3.0;  __println("r_f32_flt= " + r_f32_flt);

    // -- typed + literal - literal doesn't fit, takes default --
    int r_u8_big = bu8 + 1000;       __println("r_u8_big= " + r_u8_big);

    // -- literal + literal - both take defaults; result is untyped --
    int8  r_ll_i8  = 1 + 2;                __println("r_ll_i8= "  + r_ll_i8);
    int   r_ll_int = 100 + 200;            __println("r_ll_int= " + r_ll_int);
    int64 r_ll_big = 3_000_000_000 + 1;    __println("r_ll_big= " + r_ll_big);

    // -- negative: no common type --
    //-EXPECT-ERROR: No common type for 'uint64' and 'int8'; use an explicit type conversion.
    // int64 bad_u64_i8 = xu64 + bi8;
    //-EXPECT-ERROR: No common type for 'uint64' and 'int64'; use an explicit type conversion.
    // int64 bad_u64_i64 = xu64 + xi64;
    //-EXPECT-ERROR: No common type for 'int64' and 'float64'; use an explicit type conversion.
    // float64 bad_i64_f64 = xi64 + bf64;

    // -- negative: common type widens to float64 (int32+float32 → float64), assign narrows --
    //-EXPECT-ERROR: Cannot implicitly narrow 'float64' to 'float32'; use an explicit type conversion.
    // float32 bad_i32_f32 = bi32 + bf32;

    // -- negative: confusing-error (int32+uint32 → int64, assign narrows back to int32) --
    //-EXPECT-ERROR: Cannot implicitly narrow 'int64' to 'int32'; use an explicit type conversion.
    // int32 bad_conf = bi32 + bu32;

    // -- negative: comparison literal vs uint64 (literal -1 → int32, no common with uint64) --
    //-EXPECT-ERROR: No common type for 'uint64' and 'int32'; use an explicit type conversion.
    // bool bad_cmp = xu64 == -1;

    return 0;
}
