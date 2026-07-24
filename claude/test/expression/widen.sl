/*
test widening of the built-in primitives.

note: some of this prose is a bit stale. read claude's section.

these widening rules apply to operations where there are one or two source operands.
at least one of the operands is strongly typed - a strong operand.
its type is int8, int16, int32, int64, uint8, uint16, uint32, uint64,
float, float32, float64.
char, int, and intptr are treated as strongly typed with their implementation
dependent types - usually uint8, int32, and int64.
bool is a strange case. it can be either strong typed or weak typed -
depending on the situation.
at most one of the operands is a constant literal - a weak operand.
widening rules specific to constant literals are found in constfold/constant.sl.

constant literal type review:
constant literals have a default type determined by kind.
and a nominal type set in the constant folding stage.
kind      default  nominal
integer   int      int 8,16,32,64
                   uint 1,8,16,32,64
unsigned  uint     1,8,16,32,64
float     float    32,64
the default type is used for inference.
the nominal type is the default type plus the size.
the nominal type is used when combined with a strong type.

widening rules for complex expressions are chained together.
for example:
inferred = const int8 + ~ int32 y; -> unary ~
inferred = const int8 + int32;     -> binary +
inferred = int32;                  -> infer type

widening rules are implicit silent automatic type conversions.
explicit type conversions are not required.

general catch-all rule:
it is a compile error if any widening rule fails for any reason.
cannot be applied.
cannot be found.

these are the situation-dependent rules:

1. single source widening: parameters, expressions, return values,
overloaded function matching, template matching, etc.

1a. widen a strong operand to a known target type:

signed integers are silently widened to larger signed integers.
unsigned integers are silently widened to larger unsigned integers.
unsigned integers are silently widened to a larger unsigned integer,
then silently reinterpreted as a signed integer of the same larger size.
floating point numbers are silently widened to larger floating point numbers.
integers are silently type converted to floating point numbers for a limited
set of binary math operations: + - * / % += -= *= /= %=.
otherwise, floating point types and integer types never silently mix -
an explicit type conversion is required.

1b. widen a weak operand to a known target type:

promote the weak operand to strong using its nominal type.
apply rule 1a.

1c. a strong operand to an inferred or uknown type.
no widening.
the result type is the operand's type.

1d. a weak operand to an inferred type.
no widening.
the inferred type is the default type of the weak operand.

1e. a weak operand to an unknown type.
strange case see notes.

2. unary operations: + - ~ !

2a. unary + is a nop.
no widening.
the result type is the operand's type.

2b. unary !.
no widening - all operand types are accepted.
the result is bool.

2c. unary negation - and ~ on bool to a known target type.
the operand is widened to the target type.

2d. unary negation - and ~ on bool to an unknown target type.
strange case see notes.

2e. unary negation - and ~ on a strong type other than bool.
no widening.
the result type is the operand's type.

2f. unary operation on a weak type.
should not happen.
this should have been reduced in the constant folding stage.
compiler asserts.

3. shift operations: << >>
target = lhs << rhs;
the rhs is not widened - all types are accepted.

3a. shift operation with strong lhs to a known target type.
the rhs is widened to the target type.

3b. shift operation with weak lhs to a known target type.
the rhs is widened from its nominal type to the target type.

3c. shift operation with strong lhs to an inferred type.
no widening.
the inferred type is the type of the lhs.

3d. shift operation with weak lhs to an inferred type.
no widening.
the inferred type is the default type of the lhs.

4. comparison operations: == != <= >= < >
the result type is bool.

4a. comparison operation between two strong operands.
both operands are widened to the smallest type that will hold both types.

4b. comparison operation between one strong and one weak operand.
both operands are widened to the smallest type that will hold the
strong type and the nominal type.

4c. comparison operation between two weak operands.
should not happen.
this should have been reduced in the constant folding stage.
compiler asserts.

5. logical operations: && || ^^
no widening - all types accepted.
the result is bool.
logical operations consume condition-expressions.
0-like values are false.
everything else is true.

6. binary operations: + - / % & | ^

6a. binary operations to a known target type.
both operands are widened to the target type.

6b. binary operations on two strong types to an unknown or inferred type.
both operands are widened to the smallest type that will hold both types.
the result type is the widened type.

6c. binary operations on one strong and one weak types to an unknown or inferred type.
both operands are widened to the smallest type that will hold the
strong type and the nominal type.
the result type is the widened type.

6d. binary operations on two weak types.
should not happen.
this should have been reduced in the constant folding stage.
compiler asserts.

notes:

strange cases:
applies to weak/bool operations with an unknown target type.
either the operations should have been reduced by constant folding -
in which case, the compiler should assert.
or we're in the middle of classification -
in which case, the classifier needs to determine the type before
widening rules can be applied.
this is an error, but not a compile error.

some operations are invalid on some types.
x << float, x / 0.
these are usually handled elsewhere.

the numeric value of a literal may be outside the range of its nominal type.
the type of the numeric value of a literal is its computation type.
to widen a literal, the numeric value is truncated (or rounded) from its computation type
directly to the target type.
the numeric value is not sign or zero extended from the nominal type to the target type.
*/

/*
claude says (reconcile this spec with the new const-fold philosophy in
constfold/constant.sl):

DECIDED: char_var + char_var -> char, and the result WRAPS mod 256. a char
variable operation preserves the char kind; unlike the const rule there is no
compile-time value to fit-check, so there is no promotion-on-overflow — the
runtime value simply wraps.

notes / corrections still pending:

- char is now a first-class KIND (bool, char, integer, unsigned, float), not just
  "uint8". var widening treats it inconsistently today: char+char -> char (kept),
  char+int -> int (the no-width int), char+int8 -> int16 (char taken as a raw
  uint8: u8+s8 -> s16). document the ACTUAL behavior; "char ... treated as ...
  uint8" above is only half true.

- line ~8: int is int32 (fixed), NOT int64. only char (uint8) and intptr (int64)
  are implementation-dependent; the "usually uint8 and int64" wording wrongly
  lumps int in.

- the const+const widening rules live in constfold/constant.sl (its claude block),
  not "const/fold.sl" (line ~13). fix the cross-reference.

- no-width preference: commonType honors an explicitly-written width and otherwise
  prefers the no-width spelling (int/uint/float) when an operand contributed it
  (int+int8 -> int, not int32; int+int32 -> int32; two width-named operands stay
  width-named). rule 6b's "smallest type that holds both" should state this
  spelling precedence.

- bool: the const work settled bool as always-strong (kind bool) — it yields to
  its partner in arithmetic, stays bool under bitwise, and cannot hold > 1. the
  "bool is a strange case / can be weak / strange cases" wording (lines ~10, 1e,
  2d) can be tightened to match.
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

    // -- comparison on mixed integer-class types - result is bool --
    bool cmp_i8_i32  = bi8  < bi32;    __println("cmp_i8_i32= "  + cmp_i8_i32);
    bool cmp_u32_i32 = bu32 == bi32;   __println("cmp_u32_i32= " + cmp_u32_i32);

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
    float32 r_f32_flt = bf32 + 3.0;  __println("r_f32_flt= " + r_f32_flt);

    // -- typed + literal - literal doesn't fit, takes default --
    int r_u8_big = bu8 + 1000;       __println("r_u8_big= " + r_u8_big);

    // -- literal + literal - both take defaults; result is untyped --
    int8  r_ll_i8  = 1 + 2;                __println("r_ll_i8= "  + r_ll_i8);
    int   r_ll_int = 100 + 200;            __println("r_ll_int= " + r_ll_int);
    int64 r_ll_big = 3_000_000_000 + 1;    __println("r_ll_big= " + r_ll_big);

    // ===== strong (typed) vs weak (typeless) constants =====
    // A TYPED const is a STRONG operand — it behaves exactly like a variable of its
    // type: widens within family, and does NOT flex to fit a narrower partner. A
    // TYPELESS const is WEAK — it flexes like a bare literal.

    const int8    si8  = 4;
    const int32   si32 = 100;
    const float32 sf32 = 1.5;
    const         wk   = 5;      // typeless -> weak

    // -- strong const is a strong operand: widens within family like a variable --
    int32   c_i8_i32  = si8  + si32;   __println("c_i8_i32= "  + c_i8_i32);
    int32   c_var_i32 = bi8  + si32;   __println("c_var_i32= " + c_var_i32);
    float64 c_f32_f64 = sf32 + bf64;   __println("c_f32_f64= " + c_f32_f64);

    // -- a weak (typeless) const flexes into its partner, like a literal --
    int8 c_weak = bi8 + wk;   __println("c_weak= " + c_weak);

    // -- negative: a strong const does NOT flex to a narrow partner — the binary
    //    result keeps the const's width, so narrowing it back is rejected (a typeless
    //    const in its place would flex and compile). --
    //-EXPECT-ERROR: Cannot implicitly narrow 'int32' to 'int8'; use an explicit type conversion.
    // int8 c_narrow = bi8 + si32;
    // __println("c_narrow= " + c_narrow);

    // -- negative: same rule through a compound assignment (the bug.sl case) --
    //-EXPECT-ERROR: Cannot implicitly narrow 'int32' to 'int8'; use an explicit type conversion.
    // int8 c_aug = 0;
    // c_aug += si32;
    // __println("c_aug= " + c_aug);

    // -- negative: a strong const narrows at a plain decl like a variable would --
    //-EXPECT-ERROR: Cannot implicitly narrow 'int32' to 'int8'; use an explicit type conversion.
    // int8 c_decl = si32;
    // __println("c_decl= " + c_decl);

    // -- negative: no common type --
    // (each negative reads its local so the type error surfaces ahead of the
    //  unused-local check, which would otherwise mask it.)
    //-EXPECT-ERROR: No common type for 'uint64' and 'int8'; use an explicit type conversion.
    // int64 bad_u64_i8 = xu64 + bi8;
    // __println("bad_u64_i8= " + bad_u64_i8);
    //-EXPECT-ERROR: No common type for 'uint64' and 'int64'; use an explicit type conversion.
    // int64 bad_u64_i64 = xu64 + xi64;
    // __println("bad_u64_i64= " + bad_u64_i64);

    // -- THE ARITHMETIC CONVENIENCE (rule 1a): + - * / % silently convert an
    //    integer operand to the float operand's type; the aug twins ride. --
    float32 mix_i16_f32 = bi16 + bf32;  __println("mix_i16_f32= " + mix_i16_f32);
    float32 mix_u8_f32 = bu8 * bf32;    __println("mix_u8_f32= " + mix_u8_f32);
    float64 mix_i32_f64 = bi32 - bf64;  __println("mix_i32_f64= " + mix_i32_f64);
    float64 mix_u16_f64 = bu16 / bf64;  __println("mix_u16_f64= " + mix_u16_f64);
    float64 mix_i64_f64 = xi64 % bf64;  __println("mix_i64_f64= " + mix_i64_f64);
    float32 mix_i32_f32 = bi32 + bf32;  __println("mix_i32_f32= " + mix_i32_f32);
    float32 mix_f32_lit = bf32 + 3;     __println("mix_f32_lit= " + mix_f32_lit);
    float64 mix_aug = 1.5;
    mix_aug += bi32;                    __println("mix_aug1= " + mix_aug);
    mix_aug *= 2;                       __println("mix_aug2= " + mix_aug);

    // -- negative: the convenience is ARITHMETIC-only — bool is its own kind,
    //    comparisons keep the family wall (the equality trap stays fenced). --
    //-EXPECT-ERROR: No common type for 'bool' and 'float32'; use an explicit type conversion.
    // float32 bad_bool_f32 = xb + bf32;
    // __println("bad_bool_f32= " + bad_bool_f32);
    //-EXPECT-ERROR: No common type for 'int32' and 'float64'; use an explicit type conversion.
    // bool bad_cmp_i32_f64 = bi32 != bf64;
    // __println("bad_cmp_i32_f64= " + bad_cmp_i32_f64);
    //-EXPECT-ERROR: No common type for 'int32' and 'float32'
    // bool bad_lt_i32_f32 = bi32 < bf32;
    // __println("bad_lt_i32_f32= " + bad_lt_i32_f32);
    //-EXPECT-ERROR: Shift count must be integer-class
    // int bad_shift = bi32 << bf32;
    // __println("bad_shift= " + bad_shift);
    //-EXPECT-ERROR: No common type
    // int32 bad_aug_int = bi32;
    // bad_aug_int += bf32;
    // __println("bad_aug_int= " + bad_aug_int);

    // -- negative: confusing-error (int32+uint32 → int64, assign narrows back to int32) --
    //-EXPECT-ERROR: Cannot implicitly narrow 'int64' to 'int32'; use an explicit type conversion.
    // int32 bad_conf = bi32 + bu32;
    // __println("bad_conf= " + bad_conf);

    // -- negative: comparison literal vs uint64 (literal -1 → int32, no common with uint64) --
    //-EXPECT-ERROR: No common type for 'uint64' and 'int'; use an explicit type conversion.
    // bool bad_cmp = xu64 == -1;
    // __println("bad_cmp= " + bad_cmp);

    return 0;
}
