/*
test the built-in primitives.

types may be silently widened in many places.
they follow these rules everywhere.
including: expressions, type conversion, parameter matching, overload matching, etc.

note: for this document, literal means a literal with a flexible type.
type conversion (int8=37) will set the type of the literal.
literals with a set type are treated as non-literals.
apologies for the confusing language.
todo: fix this somehow.

the widening rules are specific to the following operations:
1. widen literal to known target type.
2. literal to inferred type.
3. unary operation on literal.
4. binary operations on two literals.
5. widen non-literal to known target type.
6. non-literal to inferred type.
7. unary operation on non-literal.
8. shift operation on non-literal to known target type.
9. shift operation on non-literal to unknown or inferred target type.
10. comparison operations.
11. binary operations on two non-literals to a known target type.
12. binary operations on two non-literals to an unknown or inferred target type.
13. binary operations on literal and non-literal to a known target type.
14. binary operations on literal and non-literal to a unknown or inferred target type.
15. logical operations.

1. widen literal to known target type.
literals may be silently widened to any type in which the value fits.
for examples:
33_000 may be any of these uint16, uint32, uint64, int32, int64, float32, float64.
-27 may be int8, int16, int32, int64, float32, float64.
floating point literals are silently rounded to match the target type precision.
3.14 becomes 3.1400001049 for target float32.
floating point literals with integer values may be silently converted to integer types
in which they fit.

2. literal to inferred type.
the inferred type is the default type of the literal.
small integer literals -> int32.
large integer literals -> int64.
small unsigned literals -> uint32.
large unsigned literals -> uint64.
small floating point literals -> float32.
large floating point literals -> float64.
bool literals -> bool.
character literals -> char.

3. unary operation on literal.
the literal is widened to its computational type.
integer -> int64,
unsigned -> uint64,
float -> float64.
the operation is performed.
exception: unary + is a nop.
exception: bitwise not ~ on bool type is ambiguous.
these are compile errors.
exception: unary - is treated as -1 * literal and is handled by rule 4.

4. binary operations on two literals.
the literals are widened to a common computational type: int64, uint64, float64.
as per rule 1.
it is a compile error if no common computational type can be found.

5. widen non-literal to known target type.
signed integers are silently widened to larger signed integers.
unsigned integers are silently widened to larger unsigned integers.
unsigned integers are silently widened to a larger unsigned integer,
then silently reinterpreted as a signed integer of the same larger size.
floating point numbers are silently widened to larger floating point numbers.
integer types may be silently converted to floating point if the entire range fits.
int16 -> float32, int32 -> float64.

6. non-literal to inferred type.
the inferred type is the type of the non-literal.

7. unary operation on non-literal.
no widening.
the result type is the type of the non-literal.

8. shift operation on non-literal to known target type.
int64 target = int32 lhs << uint8 rhs
the rhs is not widened - all types are accepted.
the lhs is widened to the target type according to rule 2.

9. shift operation on non-literal to unknown or inferred target type.
the rhs is not widened - all types are accepted.
the result and inferred type is the type of the lhs.

10. comparison operations.
the result type is bool.
the operands are widened to a type that will hold both.
the type of a non-literal lhs is tried first.
the type of a non-literal rhs is tried second.
then a common computation type will be used: int64, uint64, float64
as per rules 1 for literals and 5 for non-literals.
it is a compile error if no type can be found.

11. binary operations on two non-literals to a known target type.
both operands are widened to the target type.
it is a compile error if either cannot be widened to that type.

12. binary operations on two non-literals to an unknown or inferred target type.
use rule 10.

13. binary operations on literal and non-literal to a known target type.
both operands are widened to the target type
as per rules 1 for literals and 5 for non-literals.
it is a compile error if either cannot be widened to that type.

14. binary operations on literal and non-literal to a unknown or inferred target type.
both operands are widened to the smallest type that will hold both.
this is the result and inferred type.

15. logical operations consume condition-expressions.
no widening of operands needed - any type is accepted.

miscellaneous:

bool is a 1 bit unsigned integer.
char, float, intptr are specific to the compiler.
char is typically uint8.
intptr is typically int64.
float is typically float32.
these types should not be assumed by the author.
they are widened as per the widening rules of their implementation type.

operations involving specific literal values that are undefined are compile errors.
for examples:
shift by negative value: anything << -1
shift floating point: 3.14 << anything, anything << 3.14
divide by zero: x / 0.0

operations may be reduced:
for examples:
x * 0 -> 0
x * 1 -> x
x + 0 -> x
x / 1 -> x
x % 1 -> 0
x << 64 -> 0
x || false -> x
x && true -> x
x & 0 -> 0
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
