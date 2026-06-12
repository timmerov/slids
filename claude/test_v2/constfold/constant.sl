/*
test constants at global and function scope.

the resolve stage must push frames.
and add constants to the frames.

we must be able to fold the rhs constant expression.
then we must be able to use the named constant
to fold other constant expressions.

what is the type of a constant?

constants with an explicit type are that const type.

    const int8 kFortyTwo = 42;

type of kFortyTwo is "const int8".

constants without an explicit type infer their type from the rhs const-expression.
that type may be numeric literal.

    const kForty = kFortyTwo - 2;
    const kSeventeen = 17;

the type of kForty is "const int8".
the type of kSeventeen is "const int" but under the hood it is a numeric literal
with nominal type int8.
kForty follows the widening rules for strong types.
kSeventeen follows the widening rules for weak types.

named constants and literals have a kind: bool, char, integer, unsigned, float
they have a value.
they have an optional strong type: bool, char, int/uint 8/16/32/64, float 32/64.
if they don't have a strong type then they have a nominal type that is the
narrowest weak sized type that will hold the value: int8, uint8, int16, uint16,
int32, uint32, int64, uint64, float32, float64.

after constant folding:
kind is to be preserved whenever possible.
strong type is to be preserved whenever possible.
kind and strong/nominal type must be consistent with value.
consistent sensible things happen when different kinds and types are mixed.
*/

/*
claude says:

a folded constant equals the SAME expression on VARIABLES — const-fold and the
variable path share widen::commonType, so they cannot diverge. two buckets:

- STRONG (fixed-width): char (8), bool (1), intptr (64), or a typed intN/uintN
  const. A strong result keeps its kind and TRUNCATES the value to its width
  (wraps, register semantics).
- WEAK (no-width int/uint/float literal): flexes into a strong partner if its
  value fits, else widens by value (int -> int64 -> uint64).

result TYPE: flex a weak literal into the strong partner if it fits, then take the
common type (commonType, same as variables). With ANY strong operand the result is
fixed-width and TRUNCATES the value to that width; two weak operands widen by value.
- char + char -> char; int8 + int32 -> int32; int8 + uint8 -> int16 (a sign mix
  WIDENS via commonType -- it does NOT drop to flex).
- char + a weak literal -> char if the literal fits char, else the widened literal
  type (so 'A'+1 -> char, 'A'+1000 -> int).
- a strong int64 + uint64 (a >64-bit sign mix with no common type) is a COMPILE
  ERROR -- same as the variable path, not a silent fold.
KIND (char preferred / bool yields / sign mix -> integer) falls out of commonType's
width + sign rules.

operation-specific:
- comparison (== != < <= > >=) and logical (&& || ^^) -> bool.
- shift (<< >>) -> the kind/type of the LEFT operand, truncated to its width if
  strong; the right operand need only be integer-class.
- unary ~ is WIDTH-PRESERVING: complements within the operand's kind, KEEPING the
  kind (bool stays bool); masks to a strong operand's declared width, or a weak
  no-width literal's 64-bit computation width.
- a value that fits no integer type (> uint64) is a compile error.

examples:
  'A' + 1                  -> char 'B'           (66 fits char; weak 1 flexes in)
  'A' + 1000               -> int 1065           (1000 weak, exceeds char -> widens)
  'A' - 'B'                -> char 255            (char-char, -1 truncated to uint8)
  'a' * 'a'                -> char 193            (9409 truncated to uint8)
  'A' << 8                 -> char 0              (16640 truncated to uint8)
  true + true              -> bool false          (2 truncated to 1 bit)
  const int8 a;  a + 1     -> int8 6              (1 flexes into int8)
  const int8 a;  a + 200   -> int 205            (200 weak, exceeds int8 -> widens)
  const int8 + const uint8 -> int16              (sign mix widens via commonType)
  5u - 3u                  -> uint 2
  3u - 5u                  -> uint 4294967294     (both strong uint -> truncates/wraps)
  ~'A'                     -> char 190            (~65 @ uint8)
  ~0xFF                    -> uint64              (weak no-width -> 64-bit complement)
*/

const float kPi = 2.14 + 1.0;
const int kFortyTwo = 6 * 7;
const float64 kPlanck = 6.626;
const bool kAlive = true;
const char kStar = '*';
const char kBeta = 'A' + 1;      // char arithmetic stays char -> 'B'
const int kNegSeven = -7;
const int8 kByte = 5;
const int kShadowMe = 10;        // shadowed by a function-scope const below

int32 foo() {
    const float kTau = 2.0 * kPi;
    const int kForty = kFortyTwo - 2;
    __println(##type(kTau) + " kTau = " + kTau);
    __println(##type(kForty) + " kForty = " + kForty);
    return 0;
}

int32 bar() {
    foo();
    return 0;
}

int32 main() {

    /* compile errors — out-of-scope refs to foo()'s local consts */

    //-EXPECT-ERROR: Unresolved identifier 'kTau'
    //__println("kTau = " + kTau);

    //-EXPECT-ERROR: Unresolved identifier 'kForty'
    //__println("kForty = " + kForty);

    /* compile errors — write to a constant */

    //-EXPECT-ERROR: Cannot assign to constant
    //kPi = 4.0;

    /* compile errors — missing initializer on a const decl */

    //-EXPECT-ERROR: Constant declaration requires an initializer
    //const int kNoInit;

    /* compile errors — value out of range for the declared type */

    //-EXPECT-ERROR: does not fit declared type 'int8'
    //const int8 kTooBig = 200;

    /* a char-typed const whose value exceeds char range is rejected. */

    //-EXPECT-ERROR: does not fit declared type 'char'
    //const char kCharBig = 256;

    /* a negative literal does not fit an unsigned const (the sign cell, distinct
       from the magnitude overflow above). */

    //-EXPECT-ERROR: does not fit declared type 'uint8'
    //const uint8 kNegU = -1;

    /* compile errors — cross-family literal into a typed const: there is no
       int-literal -> float conversion, nor float-literal -> int. */

    //-EXPECT-ERROR: does not fit declared type 'float'
    //const float kIntToFloat = 5;

    //-EXPECT-ERROR: does not fit declared type 'int'
    //const int kFloatToInt = 3.5;

    /* compile error — the declared type name is not a type. */

    //-EXPECT-ERROR: Unknown type 'Bogus'
    //const Bogus kBadType = 5;

    /* a strong const is a TYPED value at use sites: narrowing it is rejected like
       a variable of its type (kFortyTwo is `const int`; int -> int8 narrows),
       even though 42 fits int8. A weak const would flex. */
    //-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
    //int8 kNarrow = kFortyTwo;
    //__println("kNarrow = " + kNarrow);

    /* compile errors — duplicate const decl in the same frame */

    //-EXPECT-ERROR: Duplicate declaration of 'kDupe'
    //const int kDupe = 1;
    //const int kDupe = 2;

    /* compile errors — cyclic const dependency between two consts */

    //-EXPECT-ERROR: Initializer for 'kCycle1' is not a constant expression
    //const int kCycle1 = kCycle2 + 1;
    //const int kCycle2 = kCycle1 + 1;

    /* compile errors — const refers to itself */

    //-EXPECT-ERROR: Initializer for 'kSelf' is not a constant expression
    //const int kSelf = kSelf + 1;

    /* compile errors — const initialized from a non-const local */

    //-EXPECT-ERROR: Initializer for 'kFromVar' is not a constant expression
    //int xVar = 42;
    //const int kFromVar = xVar + 1;

    /* a strong const + a WEAK literal that exceeds the const's narrow type: the
       literal can't flex in, so the result widens to the literal's type — kByte is
       strong int8, 200 doesn't fit int8, so the result is int 205 (no error). A
       DECLARED int8 = 200 is still a hard error (see kTooBig above). */
    const kDemote = kByte + 200;          // 200 weak, exceeds int8 -> widens to int
    __println(##type(kDemote) + " kDemote = " + kDemote);   // const int 205

    /* compile errors — typeless const initialized from a non-constant local */

    //-EXPECT-ERROR: Initializer for 'kBad' is not a constant expression
    //int yVar = 7;
    //const kBad = yVar + 1;

    /* compile error — a float mixed with an integer-class operand has no common
       type (the float / integer-class fold is rejected). */

    //-EXPECT-ERROR: No common type for floating-point and integer-class literals
    //const kFloatIntMix = 3.5 + 2;

    /* compile error — two STRONG integer consts whose common type would exceed 64
       bits (a signed/unsigned 64-bit mix): rejected, same as the variable path. */

    //-EXPECT-ERROR: No common type for 'int64' and 'uint64'
    //const int64  kNcI64 = 5;
    //const uint64 kNcU64 = 3;
    //const kNoCommon = kNcI64 + kNcU64;

    /* a fold whose value exceeds uint64 fits no integer type and is a compile
       error (operand itself past int64 -> the uint64 arithmetic overflows). */

    //-EXPECT-ERROR: Integer overflow in a folded constant expression
    //const kU64Overflow = 18446744073709551615 + 1;

    /* the same overflow reached the other way: both operands fit int64 but the
       product exceeds uint64. */

    //-EXPECT-ERROR: Integer overflow in a folded constant expression
    //const kMulOverflow = 9223372036854775807 * 3;

    bar();

    const float kPi2 = kPi / 2.0;
    const int kFortyFour = kFortyTwo + 2;
    __println(##type(kPi2) + " kPi2 = " + kPi2);
    __println(##type(kFortyFour) + " kFortyFour = " + kFortyFour);

    /* a const read inside an expression strips const -> the bare underlying. */
    __println("kFortyTwo+2 : " + ##type(kFortyTwo + 2));   // int (const dropped)

    /* a bare literal expression reports the no-width preferred spelling (int /
       uint / float / char), like a const/inferred init — a DECLARED-width value
       keeps its width name. char-arith stays char when the folded value fits,
       else promotes to int. (##type folds literal-only subtrees but never
       substitutes a const, so kFortyTwo above still reads as the const's type.) */
    __println("1 : "        + ##type(1));                  // int
    __println("0xFF : "     + ##type(0xFF));               // uint
    __println("3.5 : "      + ##type(3.5));                // float
    __println("'A'+1 : "    + ##type('A' + 1));            // char
    __println("'A'+1000 : " + ##type('A' + 1000));         // int (overflow)

    /* compile error — cyclic const dependency (kThree → kOne → kThree) */

    const int kThree = kOne + kTwo;
    const int kTwo = kOne * 2;
    const int kOne = 3*3 - 2*2*2;
    __println(##type(kThree) + " kThree = " + kThree);

    /* additional positives — broader kind coverage from file scope */

    __println(##type(kPlanck) + " kPlanck = " + kPlanck);
    __println(##type(kAlive) + " kAlive = " + kAlive);
    __println(##type(kStar) + " kStar = " + kStar);
    __println(##type(kBeta) + " kBeta = " + kBeta);
    __println(##type(kNegSeven) + " kNegSeven = " + kNegSeven);

    /* forward ref within function body — kSum sees kA and kB declared later */

    const int kSum = kA + kB;
    const int kA = 10;
    const int kB = 20;
    __println(##type(kSum) + " kSum = " + kSum);

    /* inferred-type constants — the type comes from the rhs const-expression.
       STRONG when the rhs references a typed const (the inferred const takes that
       const's type); WEAK when the rhs is a bare literal (a named literal:
       presents at the preferred spelling, narrowest nominal under the hood).
       kInferByte and kInferWeak both hold values that fit int8, but the strong
       one reads 'const int8' and the weak one 'const int'. */
    const kInferInt  = kFortyTwo - 2;     // strong: kFortyTwo is const int
    const kInferByte = kByte + 1;         // strong: kByte is const int8
    const kInferWeak = 17;                // weak:   a bare literal
    __println(##type(kInferInt)  + " kInferInt = "  + kInferInt);
    __println(##type(kInferByte) + " kInferByte = " + kInferByte);
    __println(##type(kInferWeak) + " kInferWeak = " + kInferWeak);

    /* weak literal-kind matrix — a bare-literal const presents the preferred
       default spelling for its kind. */
    const kWfloat = 3.5;
    const kWchar  = 'x';
    const kWbool  = true;
    const kWuint  = 0xFF;
    const kWbig   = 5000000000;
    __println(##type(kWfloat) + " kWfloat = " + kWfloat);
    __println(##type(kWchar)  + " kWchar = "  + kWchar);
    __println(##type(kWbool)  + " kWbool = "  + kWbool);
    __println(##type(kWuint)  + " kWuint = "  + kWuint);
    __println(##type(kWbig)   + " kWbig = "   + kWbig);

    /* strong inference variety: strong float; strong+strong (-> common type); a
       strength chain; and unary on a strong const. */
    const kSfloat = kPi + 1.0;            // strong: kPi is const float
    const kSS     = kByte + kFortyTwo;    // strong+strong: int8 + int -> int
    const kChainA = kByte;                // strong int8 (direct copy)
    const kChainB = kChainA + 1;          // strong int8 (chained through kChainA)
    const kNeg    = -kByte;               // strong int8 (unary keeps strength)
    __println(##type(kSfloat) + " kSfloat = " + kSfloat);
    __println(##type(kSS)     + " kSS = "     + kSS);
    __println(##type(kChainA) + " kChainA = " + kChainA);
    __println(##type(kChainB) + " kChainB = " + kChainB);
    __println(##type(kNeg)    + " kNeg = "    + kNeg);

    /* two strong consts of the SAME sign take their common type (explicit width
       honored, no-width slot when an operand contributed it). A sign MIX drops
       strength to flex (kWP5). */
    const int32 kI32 = 100;
    const int64 kI64 = 100;
    const uint8 kU8  = 100;
    const uint  kU   = 100;
    const kWP1 = kByte + kI32;             // int8  + int32 -> int32 (explicit width)
    const kWP2 = kFortyTwo + kI64;         // int   + int64 -> int64 (explicit width)
    const kWP3 = kFortyTwo + kI32;         // int   + int32 -> int32 (author's width wins)
    const kWP4 = kU8 + kU;                 // uint8 + uint  -> uint  (no-width slot)
    const kWP5 = kByte + kU8;              // int8  + uint8 -> int16 (sign mix widens via commonType)
    __println(##type(kWP1) + " kWP1 = " + kWP1);
    __println(##type(kWP2) + " kWP2 = " + kWP2);
    __println(##type(kWP3) + " kWP3 = " + kWP3);
    __println(##type(kWP4) + " kWP4 = " + kWP4);
    __println(##type(kWP5) + " kWP5 = " + kWP5);

    /* char arithmetic keeps char and TRUNCATES the value to uint8 (char OP char,
       or char OP a weak literal that fits char). A char OP a weak literal that
       EXCEEDS char widens to the literal's type instead (kChBig). */
    const kChFit  = 'A' + 1;       // 66   fits -> char 'B'
    const kChDown = 'a' - 32;      // 65   fits -> char 'A'
    const kChAnd  = 'C' & 'A';     // 65   fits -> char 'A'
    const kChShr  = 'B' >> 1;      // 33   fits -> char '!'
    const kChBig  = 'A' + 1000;    // 1000 weak, exceeds char -> widens to int 1065
    const kChMul  = 'a' * 'a';     // 9409 truncated to uint8 -> char 193
    const kChShl  = 'A' << 8;      // 16640 truncated to uint8 -> char 0
    __println(##type(kChFit)  + " kChFit = "  + kChFit);
    __println(##type(kChDown) + " kChDown = " + kChDown);
    __println(##type(kChAnd)  + " kChAnd = "  + kChAnd);
    __println(##type(kChShr)  + " kChShr = "  + kChShr);
    __println(##type(kChBig)  + " kChBig = "  + kChBig);
    __println(##type(kChMul)  + " kChMul = "  + kChMul);
    __println(##type(kChShl)  + " kChShl = "  + kChShl);

    /* strong unsigned subtraction truncates/wraps (a negative result becomes a
       large unsigned, like the variable path); bool arithmetic truncates to 1 bit
       (true+true -> false), and a bitwise bool result stays bool. */
    const uint kU5 = 5;
    const uint kU3 = 3;
    const kSubPos  = kU5 - kU3;     // 2  fits unsigned -> unsigned
    const kSubNeg  = kU3 - kU5;     // -2 truncates to uint -> 4294967294
    const kBoolAnd = true & false;  // 0/1              -> bool
    const kBoolAdd = true + true;   // 2 truncates to bool -> false
    __println(##type(kSubPos)  + " kSubPos = "  + kSubPos);
    __println(##type(kSubNeg)  + " kSubNeg = "  + kSubNeg);
    __println(##type(kBoolAnd) + " kBoolAnd = " + kBoolAnd);
    __println(##type(kBoolAdd) + " kBoolAdd = " + kBoolAdd);

    /* a strong const WIDENS within family like a typed value (kFortyTwo is int ->
       int64 widens); narrowing it is rejected (see the kNarrow negative). */
    int64 kWide = kFortyTwo;
    __println("kWide = " + kWide);   // 42

    /* comparison operators fold to a bool const (integer and float operands;
       spec: == != < <= > >= -> bool). */
    const kCmpLt = 5 < 3;       // false
    const kCmpLe = 3 <= 3;      // true
    const kCmpGt = 5 > 3;       // true
    const kCmpGe = 7 >= 7;      // true
    const kCmpEq = 5 == 5;      // true
    const kCmpNe = 4 != 4;      // false
    const kCmpF  = 3.5 < 4.0;   // true  (float comparison)
    __println(##type(kCmpLt) + " kCmpLt = " + kCmpLt);
    __println(##type(kCmpLe) + " kCmpLe = " + kCmpLe);
    __println(##type(kCmpGt) + " kCmpGt = " + kCmpGt);
    __println(##type(kCmpGe) + " kCmpGe = " + kCmpGe);
    __println(##type(kCmpEq) + " kCmpEq = " + kCmpEq);
    __println(##type(kCmpNe) + " kCmpNe = " + kCmpNe);
    __println(##type(kCmpF)  + " kCmpF = "  + kCmpF);

    /* logical operators (&& || ^^ and unary !) apply to ALL literal kinds plus
       nullptr: zero-like is false, not-zero-like is true; the result is a bool.
       both operands are constant, so there is nothing to short-circuit past. */
    const kLAnd   = true && false;   // false
    const kLOr    = false || true;   // true
    const kLXor   = true ^^ true;    // false
    const kLInt   = 5 && 3;          // int operands     -> true
    const kLZero  = 0 || 0;          // both zero         -> false
    const kLFlt   = 3.5 || 0.0;      // float operands    -> true
    const kLChar  = 'A' ^^ 0;        // char ^^ int       -> true
    const kLPtr   = nullptr && 5;    // nullptr zero-like -> false
    const kNotF   = !3.5;            // float not-zero    -> false
    const kNotZero= !0;              // int zero          -> true
    const kNotNull= !nullptr;        // nullptr zero-like -> true
    __println(##type(kLAnd)    + " kLAnd = "    + kLAnd);
    __println(##type(kLOr)     + " kLOr = "     + kLOr);
    __println(##type(kLXor)    + " kLXor = "    + kLXor);
    __println(##type(kLInt)    + " kLInt = "    + kLInt);
    __println(##type(kLZero)   + " kLZero = "   + kLZero);
    __println(##type(kLFlt)    + " kLFlt = "    + kLFlt);
    __println(##type(kLChar)   + " kLChar = "   + kLChar);
    __println(##type(kLPtr)    + " kLPtr = "    + kLPtr);
    __println(##type(kNotF)    + " kNotF = "    + kNotF);
    __println(##type(kNotZero) + " kNotZero = " + kNotZero);
    __println(##type(kNotNull) + " kNotNull = " + kNotNull);

    /* bool yields to a non-bool partner: the result takes the partner's kind
       (spec: bool + K -> K). bool + char -> char, bool + int -> int. */
    const kBoolChar = true + 'A';   // char 'B'
    const kBoolIntP = false + 5;    // int 5
    __println(##type(kBoolChar) + " kBoolChar = " + kBoolChar);
    __println(##type(kBoolIntP) + " kBoolIntP = " + kBoolIntP);

    /* a char arithmetic result that goes NEGATIVE promotes to int (the value-fit
       rule's negative side; kChDown above only covered a positive in-range
       result). */
    const kChNeg = 'A' - 'B';       // -1 truncated to uint8 -> char 255
    __println(##type(kChNeg) + " kChNeg = " + kChNeg);

    /* strong + the SAME strong type (same width) keeps that strong type when the
       value fits. */
    const int8 kE8a = 5;
    const int8 kE8b = 6;
    const kSameStrong = kE8a + kE8b;   // int8 11
    __println(##type(kSameStrong) + " kSameStrong = " + kSameStrong);

    /* a fold whose value exceeds int64 max promotes the KIND to unsigned
       (integer -> unsigned value-fit rule) -> uint64. */
    const kToU64 = 9223372036854775807 + 9223372036854775807;   // uint64
    __println(##type(kToU64) + " kToU64 = " + kToU64);

    /* strong + strong whose value overflows the result width TRUNCATES (wraps) to
       that width — register semantics, not a flex drop. */
    const int16 kO16a = 30000;
    const int16 kO16b = 30000;
    const kOverLarge = kO16a + kO16b;   // int16+int16, 60000 truncates int16 -> -5536
    __println(##type(kOverLarge) + " kOverLarge = " + kOverLarge);

    /* shift takes the kind/type of the LEFT operand; the right operand need only
       be integer-class (here a uint8 count into an int32 base). */
    const uint8 kShCount = 2;
    const int32 kShBase  = 1;
    const kShiftMix = kShBase << kShCount;   // int32 4
    __println(##type(kShiftMix) + " kShiftMix = " + kShiftMix);

    /* ---- declaration mechanics ---- */

    /* a const declared inside a nested block is scoped to that block. */
    if (true) {
        const int kBlk = 5;
        __println(##type(kBlk) + " kBlk = " + kBlk);
    }

    /* a function-scope const SHADOWS a file-scope const of the same name (not a
       duplicate — kShadowMe is `const int = 10` at file scope). */
    const int kShadowMe = 99;
    __println(##type(kShadowMe) + " kShadowMe = " + kShadowMe);

    /* an explicit wide type takes a small literal (it widens into the type). */
    const int64 kWideLit = 5;
    __println(##type(kWideLit) + " kWideLit = " + kWideLit);

    /* a char const takes a numeric literal (the int literal flexes into char). */
    const char kCharNum = 65;            // 'A'
    __println(##type(kCharNum) + " kCharNum = " + kCharNum);

    /* boundary values fit their declared type exactly. */
    const int8  kMin = -128;
    const uint8 kMax = 255;
    __println(##type(kMin) + " kMin = " + kMin);
    __println(##type(kMax) + " kMax = " + kMax);

    /* unary `~` complements within the operand's KIND: a STRONG fixed-width operand
       (char=8, bool=1) at that width; a WEAK no-width literal (`0xFF`) at the 64-bit
       computation width. The kind is kept (bool stays bool). */
    const kNotChar = ~'A';        // ~65 @ uint8 = 190 -> char
    const kNotBool = ~true;       // ~1  @ 1 bit  = 0   -> bool false
    const kNotUint = ~0xFF;       // weak no-width -> 64-bit complement -> uint64
    __println(##type(kNotChar) + " kNotChar = " + kNotChar);
    __println(##type(kNotBool) + " kNotBool = " + kNotBool);
    __println(##type(kNotUint) + " kNotUint = " + kNotUint);

    /* `~` on a STRONG (typed) const complements at that const's DECLARED width and
       keeps its type — distinct from the weak `~0xFF` above (64-bit). */
    const uint32 kU32a = 0xFF;
    const int8   kI8a  = 5;
    const uint8  kU8z  = 0;
    const kNotU32 = ~kU32a;       // ~0xFF @ 32 -> uint32 4294967040
    const kNotI8  = ~kI8a;        // ~5    @ 8  -> int8 -6
    const kNotU8  = ~kU8z;        // ~0    @ 8  -> uint8 255
    __println(##type(kNotU32) + " kNotU32 = " + kNotU32);
    __println(##type(kNotI8)  + " kNotI8 = "  + kNotI8);
    __println(##type(kNotU8)  + " kNotU8 = "  + kNotU8);

    /* strong + strong whose value OVERFLOWS the result width truncates (wraps) to
       that width — register semantics, on the smallest widths (kOverLarge above
       covers int16). */
    const int8  kI8x = 100;
    const int8  kI8y = 100;
    const uint8 kU8x = 200;
    const uint8 kU8y = 100;
    const kI8Wrap = kI8x + kI8y;   // 200 wraps int8  -> -56
    const kU8Wrap = kU8x + kU8y;   // 300 wraps uint8 -> 44
    __println(##type(kI8Wrap) + " kI8Wrap = " + kI8Wrap);
    __println(##type(kU8Wrap) + " kU8Wrap = " + kU8Wrap);

    return 0;
}

/* a strong const is a typed value at the OTHER assignment sites too — narrowing
   it is rejected at a return, a call argument, and a store (kFortyTwo is the
   file-scope `const int`). */

//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int8 neg_return() { return kFortyTwo; }

//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int8 neg_arg_helper(int8 x) { return x; }
//int32 neg_arg() { neg_arg_helper(kFortyTwo); return 0; }

//-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
//int32 neg_store() { int8 arr[2]; arr[0] = kFortyTwo; return arr[0]; }
