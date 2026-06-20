/*
test type conversion aka value conversion

conceptually type conversion is an assignment to a temporary variable.
parentheses are required.
they type must be value type.
it may not be a pointer type.
the expression may be a pointer when the type is bool, intptr,
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

    /* ---- signed/unsigned float<->int variant (output-locked, not just emitted) ---- */

    uint8 uhi8 = 200;                   // 0xC8, high bit set
    float64 ufhi = (float64 = uhi8);    // uitofp -> 200 (sitofp would read it as -56)
    __println("ufhi = " + ufhi);        // 200

    float64 ufval = 3000000000.0;       // in the uint range, beyond signed int
    uint uconv = (uint = ufval);        // fptoui -> 3000000000 (a value only unsigned holds)
    __println("uconv = " + uconv);      // 3000000000

    int negfl = (int = -3.9);           // fold: truncate toward zero -> -3 (not floor -4)
    __println("negfl = " + negfl);      // -3
    float nf = -3.9;
    int negfr = (int = nf);             // runtime fptosi toward zero -> -3
    __println("negfr = " + negfr);      // -3

    /* ---- char / bool as a runtime conversion source ---- */

    char chs = 'A';
    int chi = (int = chs);              // runtime char (uint8) source -> 65
    __println("chi = " + chi);          // 65
    bool bsrc = true;
    int bis = (int = bsrc);             // runtime bool source -> 1
    float64 bfs = (float64 = bsrc);     // runtime bool source -> 1.0
    __println("bis = " + bis + " bfs = " + bfs);   // 1 1

    /* tuples — per-slot conversion. */
    (int,int) tpl1 = (65,66);
    tpl2 = ((char,char)=tpl1);
    __println("tpl2 = (" + tpl2[0] + "," + tpl2[1] + ")");                   // (A,B)

    /* arrays — per-element conversion. char[3] -> int[3] via the leaf grid. */
    char ch_arr[3] = ('a', 'b', 'c');
    int int_arr[3] = (int[3] = ch_arr);
    __println("int_arr = " + int_arr[0] + " " + int_arr[1] + " " + int_arr[2]);  // 97 98 99

    /* a const-EXPRESSION dim baked into the conversion target. */
    const int kC = 3;
    char ch_kc[kC] = (65, 66, 67);
    int int_kc[kC] = (int[kC] = ch_kc);
    __println("int_kc = " + int_kc[0] + " " + int_kc[1] + " " + int_kc[2]);  // 65 66 67

    /* nested: tuple-of-tuple. */
    ((int,int),(int,int)) ntpl = ((65,66), (67,68));
    n2 = (((char,char),(char,char))=ntpl);
    __println("n2 = ((" + n2[0][0] + "," + n2[0][1] + "),("
              + n2[1][0] + "," + n2[1][1] + "))");                           // ((A,B),(C,D))

    /* nested: tuple-with-array slot. */
    (int, int[2]) tpa = (90, (97, 98));
    tpa2 = ((char, char[2]) = tpa);
    __println("tpa2 = (" + tpa2[0] + ", [" + tpa2[1][0] + "," + tpa2[1][1] + "])"); // (Z, [a,b])

    /* multi-dim array. */
    char m2[2][3] = (('a','b','c'), ('d','e','f'));
    int im2[2][3] = (int[2][3] = m2);
    __println("im2 = " + im2[0][0] + " " + im2[1][2]);                       // 97 102

    /* array-of-tuple — the symmetric direction to tuple-of-array. The codegen
       array arm peels one outer dim and recurses into the tuple elem. */
    (char,char) at_arr[2] = ((65,66), (67,68));
    aoi = ((int,int)[2] = at_arr);
    __println("aoi = (" + aoi[0][0] + "," + aoi[0][1] + ") ("
              + aoi[1][0] + "," + aoi[1][1] + ")");                          // (65,66) (67,68)

    /* CROSS-FORM conversion — an array IS a homogeneous tuple, so the source need
       not match the target's form: a tuple converts to an array target and an array
       to a tuple target (slot count must match; the leaf still uses the explicit
       grid, so per-leaf narrowing is allowed). */
    int xfca[2] = (int[2] = (1, 2));          // tuple literal -> array
    __println("xfca = " + xfca[0] + " " + xfca[1]);                          // 1 2
    int xfav[3] = (4, 5, 6);
    xfct = ((int,int,int) = xfav);            // array value -> tuple
    __println("xfct = (" + xfct[0] + "," + xfct[1] + "," + xfct[2] + ")");   // (4,5,6)
    (int[2], int[2]) xftoa = ((1,2), (3,4));
    xfaot = ((int,int)[2] = xftoa);           // tuple-of-arrays -> array-of-tuples
    __println("xfaot = " + xfaot[0][0] + " " + xfaot[0][1] + " "
              + xfaot[1][0] + " " + xfaot[1][1]);                            // 1 2 3 4
    xfcn = (int8[2] = (300, 2));              // cross-form + explicit per-leaf narrow
    __println("xfcn = " + xfcn[0] + " " + xfcn[1]);                          // 44 2

    /* pointer-leaf in a tuple slot (positive): each pointer source converts to
       bool or intptr per the existing leaf rule, applied through the per-slot
       walk. */
    int vp = 42;
    int^ rvp = ^vp;
    int^ nulp = nullptr;
    pirv = ((bool, intptr) = (rvp, nulp));
    __println("pirv = " + pirv[0] + " " + pirv[1]);                          // true 0

    /* iterator source in a tuple slot — kIterator is pointer-like, same leaf
       rule. */
    int harr2[3];
    harr2[0] = 1;
    int[] it2 = ^harr2[0];
    bib = ((bool, bool) = (it2, it2));
    __println("bib = " + bib[0] + " " + bib[1]);                             // true true

    /* const-EXPRESSION dim baked into a tuple SLOT type (the dim_sink flows
       through the tuple-slot recursion in parseType). */
    (char[kC], char) src_kt = ((65,66,67), 90);
    tup_kt = ((int[kC], int) = src_kt);
    __println("tup_kt = (" + tup_kt[0][0] + " " + tup_kt[0][1] + " "
              + tup_kt[0][2] + ", " + tup_kt[1] + ")");                      // (65 66 67, 90)

    /* cross-category in slots — float -> int per slot, exercising the leaf
       grid through the tuple walk. */
    (float, float) ff = (1.5, 2.7);
    cci = ((int, int) = ff);
    __println("cci = (" + cci[0] + "," + cci[1] + ")");                      // (1,2)

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

    /* a CROSS-FORM tuple<->array conversion is allowed (an array is a homogeneous
       tuple), but a SCALAR into an aggregate target is not — aggregate-vs-scalar. */
    //-EXPECT-ERROR: Cannot convert 'int' to '(char, char)'
    //int neg_tuple_target_scalar_src() {
    //    int s = 5;
    //    tpl_es = ((char,char) = s);
    //    return tpl_es[0];
    //}

    /* an aggregate conversion source must match the slot/element COUNT (cross-form
       array<->tuple is allowed; only a count mismatch rejects). */
    //-EXPECT-ERROR: slot count differs
    //int neg_array_dim_mismatch() {
    //    char c[4] = ('a', 'b', 'c', 'd');
    //    int a[3] = (int[3] = c);
    //    return a[0];
    //}

    /* a tuple conversion source must be a tuple of the SAME arity. */
    //-EXPECT-ERROR: slot count differs
    //int neg_tuple_arity_mismatch() {
    //    (int, int, int) t = (1, 2, 3);
    //    pair = ((char, char) = t);
    //    return pair[0];
    //}

    /* a SCALAR source into an aggregate (array) target is rejected — the cross-form
       allowance is array<->tuple only, not scalar<->aggregate. */
    //-EXPECT-ERROR: Cannot convert 'int' to 'int[3]'
    //int neg_array_target_scalar_src() {
    //    int s = 5;
    //    int arr_es[3] = (int[3] = s);
    //    return arr_es[0];
    //}

    /* pointer leaf in a tuple slot whose target is neither bool nor intptr —
       the recursive walk reaches the leaf rule and rejects per slot. */
    //-EXPECT-ERROR: a pointer converts only to 'bool' or 'intptr'
    //int neg_ptr_in_slot_non_bool() {
    //    int^ pn = nullptr;
    //    bad = ((int, int) = (pn, pn));
    //    return bad[0];
    //}

    /* inner tuple arity mismatch — the outer arity matches but a nested slot's
       arity differs; the recursive walk fires inside, attributed at the outer
       conv tok (per-slot caret refinement is a later cleanup). */
    //-EXPECT-ERROR: slot count differs
    //int neg_inner_tuple_arity() {
    //    inner_bad = (((char,char),(char,char)) = ((1,2),(3,4,5)));
    //    return inner_bad[0][0];
    //}

    /* inner array shape mismatch — outer tuple arity matches but a tuple slot's
       inner array dims differ (a count mismatch at a nested level). */
    //-EXPECT-ERROR: slot count differs
    //int neg_inner_array_dim() {
    //    char a3[3] = (1,2,3);
    //    char a4[4] = (4,5,6,7);
    //    inner_bad2 = ((int[3], int[3]) = (a3, a4));
    //    return inner_bad2[0][0];
    //}

    /* A class as a conversion target is deferred until op= lands. The grammar
       has no top-level user-named conversion target, so a class would have to
       be reached through a tuple/array slot — but a tuple type with a class
       slot doesn't resolve today (separate gap), so the classify defer arm is
       unreachable from any current test. Re-add a negative when either path
       opens. */

    /* the result is a STRONG typed value, so assigning it to a narrower type is
       rejected just like an int variable would be — it does not flex. */
    //-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
    //int8  en = (int = 5);
    //__println("x= " + en);

    return 0;
}
