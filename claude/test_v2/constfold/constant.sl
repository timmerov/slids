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

these are the CONST + CONST folding rules. (var + var / var + const WIDENING
rules are different and are not described here.)

combining two constants: compute the result KIND, promote the kind to stay
consistent with the value, then settle the result TYPE (strong or flex). the
guiding idea: preserve kind and strong type whenever the value allows, else bail
to a flex type.

result KIND -- arithmetic (+ - * / %) and bitwise (& | ^), symmetric:
- float combines only with float; float mixed with any integer-class kind is a
  "no common type" error.
- bool yields to the other operand: bool + K -> K; bool + bool -> bool.
- char is preferred whenever present: char + char/integer/unsigned -> char.
- a signed + unsigned mix -> integer (signed wins).
- otherwise the shared kind: integer + integer -> integer, unsigned + unsigned
  -> unsigned.

promote the KIND to fit the value (kind must stay consistent with the value):
- bool whose value is not 0 or 1   -> integer    (true + true = 2)
- char whose value is not 0..255   -> integer    ('A' + 1000; 'A' - 'B' = -1)
- unsigned whose value is < 0      -> integer    (3u - 5u = -2)
- integer whose value > int64 max  -> unsigned    (needs uint64)
- a value that fits nothing (> uint64) is a compile error.

result TYPE -- strong vs flex:
- a bool or char result is always STRONG (bool / char).
- flex + flex -> flex (nominal: the narrowest weak type that holds the value).
- strong + flex, or strong + the same strong type -> that strong type if the
  value fits it, else flex.
- strong + strong, same sign, different width -> the larger strong type if the
  value fits it, else flex.
- strong signed + strong unsigned -> flex integer (a sign mix always drops
  strength).
- if the KIND was promoted above (the value didn't fit), the result is FLEX --
  the strong type could not hold it.

operation-specific:
- comparison (== != < <= > >=) and logical (&& || ^^) -> bool.
- shift (<< >>) -> the kind and type of the LEFT operand, then the value-fit
  promotion; the right operand need only be integer-class.
- subtraction is the only op that drives a same-sign result negative; the
  unsigned -> integer promotion above covers it.
- bitwise on two bools (true & false) stays bool (the 0/1 result fits).

examples:
  'A' + 1                  -> strong char 'B'      (char, fits 0..255)
  'A' + 1000               -> flex integer 1065    (char promoted to integer)
  'a' * 'a'                -> flex integer 9409     (char promoted)
  char + unsigned-flex     -> strong char          (char preferred), then value-fit
  const int8 a;  a + 1     -> strong int8 6         (fits int8)
  const int8 a;  a + 200   -> flex integer 205      (205 exceeds int8 -> bails)
  const int8 + const int16 -> strong int16          (larger, if value fits)
  const int + const uint   -> flex integer          (sign mix drops strength)
  5u - 3u                  -> unsigned 2
  3u - 5u                  -> integer -2            (unsigned went negative)
*/

const float kPi = 2.14 + 1.0;
const int kFortyTwo = 6 * 7;
const float64 kPlanck = 6.626;
const bool kAlive = true;
const char kStar = '*';
const char kBeta = 'A' + 1;      // char arithmetic stays char -> 'B'
const int kNegSeven = -7;
const int8 kByte = 5;

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

    /* a typeless const whose strong value overflows its inferred narrow type
       DEMOTES to flex — kByte is strong int8, but 205 doesn't fit int8, so the
       result is a flex int (no error; the const-fold demotion rule). A DECLARED
       int8 that overflows is still a hard error (see kTooBig above). */
    const kDemote = kByte + 200;          // 205 exceeds int8 -> flex int
    __println(##type(kDemote) + " kDemote = " + kDemote);   // const int 205

    /* compile errors — typeless const initialized from a non-constant local */

    //-EXPECT-ERROR: Initializer for 'kBad' is not a constant expression
    //int yVar = 7;
    //const kBad = yVar + 1;

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
    const kWP5 = kByte + kU8;              // int8  + uint8 -> flex int (sign mix drops strength)
    __println(##type(kWP1) + " kWP1 = " + kWP1);
    __println(##type(kWP2) + " kWP2 = " + kWP2);
    __println(##type(kWP3) + " kWP3 = " + kWP3);
    __println(##type(kWP4) + " kWP4 = " + kWP4);
    __println(##type(kWP5) + " kWP5 = " + kWP5);

    /* char arithmetic on LITERALS stays char when the folded value fits 0..255,
       else promotes to int (char's value-dependent rule; all arith operators). */
    const kChFit  = 'A' + 1;       // 66   fits -> char 'B'
    const kChDown = 'a' - 32;      // 65   fits -> char 'A'
    const kChAnd  = 'C' & 'A';     // 65   fits -> char 'A'
    const kChShr  = 'B' >> 1;      // 33   fits -> char '!'
    const kChBig  = 'A' + 1000;    // 1065 overflows char -> int
    const kChMul  = 'a' * 'a';     // 9409 overflows char -> int
    const kChShl  = 'A' << 8;      // 16640 overflows char -> int
    __println(##type(kChFit)  + " kChFit = "  + kChFit);
    __println(##type(kChDown) + " kChDown = " + kChDown);
    __println(##type(kChAnd)  + " kChAnd = "  + kChAnd);
    __println(##type(kChShr)  + " kChShr = "  + kChShr);
    __println(##type(kChBig)  + " kChBig = "  + kChBig);
    __println(##type(kChMul)  + " kChMul = "  + kChMul);
    __println(##type(kChShl)  + " kChShl = "  + kChShl);

    /* unsigned subtraction is sign-aware (positive stays unsigned, negative ->
       integer); bool yields to its partner and promotes when it can't hold the
       value, but a bitwise bool result stays bool. */
    const uint kU5 = 5;
    const uint kU3 = 3;
    const kSubPos  = kU5 - kU3;     // 2  fits unsigned -> unsigned
    const kSubNeg  = kU3 - kU5;     // -2 negative      -> integer
    const kBoolAnd = true & false;  // 0/1              -> bool
    const kBoolAdd = true + true;   // 2 can't fit bool -> integer
    __println(##type(kSubPos)  + " kSubPos = "  + kSubPos);
    __println(##type(kSubNeg)  + " kSubNeg = "  + kSubNeg);
    __println(##type(kBoolAnd) + " kBoolAnd = " + kBoolAnd);
    __println(##type(kBoolAdd) + " kBoolAdd = " + kBoolAdd);

    return 0;
}
