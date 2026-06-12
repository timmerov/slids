/*
test type conversion aka value conversion

conceptually type conversion is an assignment to a temporary variable.
parentheses are required.
they type must be value type.
it may not be a pointer type.
the expression may be a pointer type when the type is bool, intptr,
or a class that defines the approprate assignment operator.

    (type=expr)

examples:

    int x = (int=3.14);  // truncates
    y = (uint= -37);     // reinterprets bits
    b = (bool=5);        // true
    b = (bool=nullptr);  // false
    s = (String="Hello, World!");

type conversions may be chained.

    int^ ref = nullptr;
    x = (float64 = intptr = ref);

tuples are type converted by slot iteratively and recursively.

    (char, char) ch_tpl = ('A', 'B');
    (int, int) int_tpl = ((int,int)=tpl);

    int_tpl is now (65, 66)

arrays are type converted by element iteratively and recursively.

    char ch_arr[3] = ('a', 'b', 'c);
    int int_arr[3] = (int[3]=ch_arr);

    int_arr is now [97, 98, 99]

notes:
pointer casting reinterprets the bits without changing them.
type conversion changes the bits.
*/

/*
claude says:

a conversion is the explicit escape hatch for the value changes implicit
assignment rejects (narrowing, cross-family, sign-change). it CHANGES the bits
(a cast reinterprets them). modeled as assignment-to-a-temp, so the result is a
strong typed value of the target. semantics are C across the whole value grid.

covered here:
  - literal-operand folds (constfold): float->int truncate, int->uint
    reinterpret, narrowing wrap (signed + unsigned), ->bool nonzero test,
    ->char, bool/char source.
  - runtime (non-constant) operands: same grid lowered by codegen.
  - every distinct lowering op: ->bool (icmp / fcmp nonzero), int<->int
    (sext / zext / trunc / same-width reinterpret), float<->int (fptosi /
    fptoui / sitofp / uitofp), float<->float (fpext / fptrunc).
  - wider tiers (int16 / int64 / uint64) and same-width sign reinterpret.
  - pointer / iterator source -> bool (non-null test) and -> intptr (ptrtoint).
  - chaining: (float64 = intptr = ref), right-to-left, no inner parens.
  - a folded conversion initializing a const and sizing an array dimension.
  - (negative) the strong result does not flex: int8 = (int=5) is a narrowing
    error, like assigning an int variable would be.

a pointer is the only operand whose conversion target is restricted (bool /
intptr only); every other value converts to every value type.

deferred to Phase 5 (classes):
  - (String = "...") and (Class = Type^) — class assignment-conversion.
  - a user-named type as a conversion target (only primitive type keywords lead
    a conversion in the grammar today).

addresses are nondeterministic, so the pointer->intptr cases print a stable
projection (a null address, or the bool of a non-null one) rather than the raw
value.
*/

int32 main() {

    /* ---- literal-operand folds (constfold, C semantics) ---- */

    int x = (int=3.14);                 // float -> int truncates toward zero
    __println("x = " + x);              // 3

    uint y = (uint= -37);               // reinterpret -37 as unsigned
    __println("y = " + y);              // 4294967259

    int8 narrow = (int8=300);           // 300 mod 256 -> 44
    __println("narrow = " + narrow);    // 44

    int8 wrap = (int8=200);             // 200 -> signed 8-bit -> -56
    __println("wrap = " + wrap);        // -56

    char ca = (char=65);                // code point 65
    __println("ca = " + ca);            // A

    bool bt = (bool=5);                 // nonzero -> true
    __println("bt = " + bt);            // true

    bool bz = (bool=nullptr);           // null -> false
    __println("bz = " + bz);            // false

    int ti = (int=true);                // bool source -> 1
    __println("ti = " + ti);            // 1

    /* ---- runtime (non-constant) operands ---- */

    float f = 3.9;
    int fi = (int=f);                   // 3.9 truncates toward zero
    __println("fi = " + fi);            // 3

    int neg = -1;
    uint un = (uint=neg);               // reinterpret -1 -> max uint
    __println("un = " + un);            // 4294967295

    int small = 7;
    float64 fd = (float64=small);       // int -> double 7.0
    __println("fd = " + fd);            // 7

    /* ---- same-family width changes ---- */

    int8 src8 = -5;
    int wide = (int=src8);              // sign-extend signed source
    __println("wide = " + wide);        // -5

    uint8 us = 250;
    int wu = (int=us);                  // zero-extend unsigned source
    __println("wu = " + wu);            // 250

    int big = 321;
    int8 trunc = (int8=big);            // 321 mod 256 -> 65
    __println("trunc = " + trunc);      // 65

    int code = 66;
    char cb = (char=code);              // runtime int -> char
    __println("cb = " + cb);            // B

    /* ---- pointer source: bool (non-null test) + intptr ---- */

    int^ nul = nullptr;
    bool isn = (bool=nul);              // null -> false
    __println("isn = " + isn);          // false

    int v = 9;
    int^ rv = ^v;
    bool nn = (bool=rv);                // non-null -> true
    __println("nn = " + nn);            // true

    intptr addr = (intptr=nul);         // null address -> 0
    __println("addr = " + addr);        // 0

    /* ---- chaining (right-to-left) ---- */

    float64 chain = (float64 = intptr = nul);   // ptr -> i64 0 -> 0.0
    __println("chain = " + chain);              // 0

    bool live = (bool = intptr = rv);           // ptr -> nonzero i64 -> true
    __println("live = " + live);                // true

    /* ---- the rest of the value grid (every distinct lowering op) ---- */

    bool fb  = (bool = 2.5);            // float -> bool nonzero test -> true
    __println("fb = " + fb);            // true
    bool fb0 = (bool = 0.0);            // float zero -> false
    __println("fb0 = " + fb0);          // false

    uint fu = (uint = 2.9);             // float -> unsigned (truncates) -> 2
    __println("fu = " + fu);            // 2
    float fzz = 250.7;
    uint8 fu8 = (uint8 = fzz);          // runtime float -> unsigned
    __println("fu8 = " + fu8);          // 250

    uint uu = 5;
    float64 uf = (float64 = uu);        // unsigned -> float
    __println("uf = " + uf);            // 5
    float64 tf = (float64 = true);      // bool -> float
    __println("tf = " + tf);            // 1

    float32 sf = 2.25;
    float64 df = (float64 = sf);        // float32 -> float64 (fpext)
    __println("df = " + df);            // 2.25
    float64 wide64 = 3.5;
    float32 df32 = (float32 = wide64);  // float64 -> float32 (fptrunc)
    __println("df32 = " + df32);        // 3.5

    /* ---- conversion in a const initializer + an array dimension (fold) ---- */

    const int N = (int = 3.9);          // folds to a strong int constant -> 3
    __println("N = " + N);              // 3

    int dim[(int = 2.5)];               // the dimension folds to 2
    dim[0] = 7;
    dim[1] = 8;                         // index 1 is in range iff the dim is 2
    int dsum = dim[0] + dim[1];
    __println("dsum = " + dsum);        // 15

    /* ---- wider tiers + same-width reinterpret ---- */

    uint64 u64 = (uint64 = -1);         // reinterpret -1 in 64 bits
    __println("u64 = " + u64);          // 18446744073709551615
    int64 n64 = (int64 = -5);           // sign-extended 64-bit
    __println("n64 = " + n64);          // -5
    int16 s16 = (int16 = 70000);        // 70000 mod 2^16 -> 4464
    __println("s16 = " + s16);          // 4464

    uint uw = 4294967295;
    int iw = (int = uw);                // same-width uint -> int reinterpret
    __println("iw = " + iw);            // -1

    /* ---- iterator source (not just a reference) ---- */

    int harr[3];
    harr[0] = 1;
    int[] it = ^harr[0];                // an iterator into the array
    bool itb = (bool = it);             // non-null -> true
    __println("itb = " + itb);          // true

    /* compile errors — each uncommented in isolation by the negative runner. */

    /* a pointer converts only to bool / intptr, not an arbitrary numeric. */
    //-EXPECT-ERROR: a pointer converts only to 'bool' or 'intptr'
    //int^ ep = nullptr;
    //int eq = (int = ep);
    //__println("x= " + eq);

    /* the target is never a pointer type. */
    //-EXPECT-ERROR: A type conversion target may not be a pointer type
    //int ev = 5;
    //int^ ey = (int^ = ev);
    //__println("x= " + ey^);

    /* a non-value (tuple) source has no scalar conversion. */
    //-EXPECT-ERROR: Cannot convert '(int, int)' to 'int'
    //int et = (int = (1, 2));
    //__println("x= " + et);

    /* a non-value target (void) is rejected. */
    //-EXPECT-ERROR: Cannot convert to 'void'; the target must be a value type
    //int es = 5;
    //int ed = (void = es);
    //__println("x= " + ed);

    /* the result is a STRONG typed value, so assigning it to a narrower type is
       rejected just like an int variable would be — it does not flex. */
    //-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
    //int8  en = (int = 5);
    //__println("x= " + en);

    return 0;
}
