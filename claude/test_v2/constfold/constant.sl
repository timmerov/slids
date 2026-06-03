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
*/

const float kPi = 2.14 + 1.0;
const int kFortyTwo = 6 * 7;
const float64 kPlanck = 6.626;
const bool kAlive = true;
const char kStar = '*';
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

    /* compile errors — typeless const whose STRONG value overflows the inferred
       type (the diagnostic says "inferred type", not "declared type") */

    //-EXPECT-ERROR: does not fit inferred type 'int8'
    //const kOver = kByte + 200;

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

    /* compile error — cyclic const dependency (kThree → kOne → kThree) */

    const int kThree = kOne + kTwo;
    const int kTwo = kOne * 2;
    const int kOne = 3*3 - 2*2*2;
    __println(##type(kThree) + " kThree = " + kThree);

    /* additional positives — broader kind coverage from file scope */

    __println(##type(kPlanck) + " kPlanck = " + kPlanck);
    __println(##type(kAlive) + " kAlive = " + kAlive);
    __println(##type(kStar) + " kStar = " + kStar);
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

    return 0;
}
