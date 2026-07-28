/*
test type conversion aka value conversion

conceptually type conversion is an assignment to a temporary variable.
parentheses are required.
the type must be value type.
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

a pointer is the only PRIMITIVE operand whose conversion target is restricted
(bool / intptr only); every other value converts to every value type.

conversion to a CLASS target (LANDED). the "assignment to a temp" model is literal
here: (Class = src) default-constructs a `_$cret` of the class, FILLS it from the
source, and yields the temp — the same fill a decl-init `Class x = src` would run. a
value source needs op=(T), a pointer source op=(T^) (header lines 8-9); a SAME-CLASS
source with no user op= falls to the default whole-value COPY (like `Amt x = a`); a
source that is neither op=-viable nor the same class is a clean error, no implicit
narrowing into an operator. the grammar reaches a class target via an identifier-led
conversion (looksLikeConvTarget; a top-level `=` inside `(...)` is unambiguous — slids
has no assignment-EXPRESSION). the temp is a class rvalue lifted like a construction:
destroyed at the end of the phrase (a statement-expression position) or the enclosing
scope (a decl/assign rhs). an AGGREGATE target with a class leaf converts PER SLOT
(lowerAggregateConversion desugars it to a tuple of per-slot sub-conversions). this is
the 12th declarator binding site (plan-declarator.txt). scalar coverage: amt1/amt2/amt3
(value / assign / pointer); w (a CLASS source via op=(Amt^)); inplace (field read off the
temp); amt_from (a RETURN); amt_pair (TWO in one statement); mc (a NAMESPACED member class,
single-colon); dol (an ALIAS); va (a VIRTUAL class — vptr stamped); the `if` (CONDITION —
lifts through the phrase seq, so the seq codegen discards the void op= call). aggregate
coverage: pair_cs (class-leaf tuple), sc (same-class copy), mx (mixed slots), cav (class
array), cxf/rst (cross-form reshape both ways), nag (nested), sp (SPILL — mkpair prints
once, proving single evaluation), wv (cross-class-in-slot), vv (virtual-in-slot), chc
(chained to a class), nc (class-typed-field copy). negatives: neg_class_no_op (no viable
op=), neg_class_ptr_target (a class pointer target), neg_abstract_target (the temp's
default-construct triggers the abstract check).

addresses are nondeterministic, so the pointer->intptr cases print a stable
projection (a null address, or the bool of a non-null one) rather than the raw
value.
*/

/* a class that is the TARGET of a value conversion. A conversion to a class is an
   assignment to a temporary of that class (header lines 8-9): it dispatches the
   class's op= — a value source through op=(int), a pointer source through
   op=(int^). The temp is destroyed at the end of the phrase. */
import string;

Amt(int cents_) {
    _() {}
    ~() {}
    op=(int rhs) { cents_ = rhs; }
    op=(int^ p)  { cents_ = p^; }
}

/* the same class with OBSERVABLE ctor/dtor, to show the conversion temp's lifetime.
   In a statement-expression position the `_$cret` temp is destroyed at the END OF
   THE PHRASE — the same place a post-increment side effect lands. trace_use takes
   the temp by pointer and prints it; the temp's -dtor fires before the next
   statement, not at block exit. */
Trace(int id_) {
    _() { println(String + "  +ctor " + id_); }
    ~() { println(String + "  -dtor " + id_); }
    op=(int rhs) { id_ = rhs; }
}
int trace_use(Trace^ t) {
    println(String + "  use " + t^.id_);
    return 0;
}

/* more conversion targets: a CLASS source (op= takes another class by pointer), a
   NAMESPACED member class, an ALIAS to a class, and a VIRTUAL class (its temp is
   vptr-stamped so dispatch works). Abs is abstract — a conversion-target NEGATIVE
   only, never instantiated in the positive path. */
Wrap(int w_) { _() {} ~() {} op=(Amt^ a) { w_ = a^.cents_; } }
Money {
    Cents(int c_) { _() {} ~() {} op=(int r) { c_ = r; } }
}
alias Dollars = Amt;
Vshape(int s_) { _() {} virtual ~() {} op=(int r) { s_ = r; } virtual int area() { return s_; } }
Abs(int a_) { _() {} virtual ~() {} op=(int r) { a_ = r; } virtual int f() = delete; }
/* a class with a CLASS-TYPED field — same-class copy must recurse into the inner class. */
Nested(Amt part, int label_) { _() {} ~() {} op=(int r) { part.cents_ = r; label_ = r + 1; } }
/* a tuple-returning function — a side-effecting (non-lvalue, non-literal) conversion
   source, to prove the aggregate spill evaluates the source EXACTLY ONCE (prints once). */
(int, int) mkpair() { println(String + "  mkpair"); return (50, 60); }
/* a class with an aggregate FIELD, so `hld.pr_` is a bare-lvalue tuple source
   (kFieldExpr): an aggregate conversion re-indexes it in place per slot with NO spill
   — unlike mkpair()'s call source above. Exercises the kFieldExpr arm of the shared
   bare-lvalue predicate at the aggregate-conversion spill site. */
Holder( (int, int) pr_ ) { _() {} ~() {} }

Amt amt_from(int n) { return (Amt = n); }               // a conversion at a RETURN
int amt_pair(Amt^ a, Amt^ b) {
    int s = a^.cents_ + b^.cents_;
    println(String + "pair = " + s);
    return 0;
}

int32 main() {

    /* ---- literal-operand folds (constfold, C semantics) ---- */

    int x = (int=3.14);                 // float -> int truncates toward zero
    println(String + "x = " + x);              // 3

    uint y = (uint= -37);               // reinterpret -37 as unsigned
    println(String + "y = " + y);              // 4294967259

    int8 narrow = (int8=300);           // 300 mod 256 -> 44
    println(String + "narrow = " + narrow);    // 44

    int8 wrap = (int8=200);             // 200 -> signed 8-bit -> -56
    println(String + "wrap = " + wrap);        // -56

    char ca = (char=65);                // code point 65
    println(String + "ca = " + ca);            // A

    bool bt = (bool=5);                 // nonzero -> true
    println(String + "bt = " + bt);            // true

    bool bz = (bool=nullptr);           // null -> false
    println(String + "bz = " + bz);            // false

    int ti = (int=true);                // bool source -> 1
    println(String + "ti = " + ti);            // 1

    /* ---- runtime (non-constant) operands ---- */

    float f = 3.9;
    int fi = (int=f);                   // 3.9 truncates toward zero
    println(String + "fi = " + fi);            // 3

    int neg = -1;
    uint un = (uint=neg);               // reinterpret -1 -> max uint
    println(String + "un = " + un);            // 4294967295

    int small = 7;
    float64 fd = (float64=small);       // int -> double 7.0
    println(String + "fd = " + fd);            // 7

    /* ---- same-family width changes ---- */

    int8 src8 = -5;
    int wide = (int=src8);              // sign-extend signed source
    println(String + "wide = " + wide);        // -5

    uint8 us = 250;
    int wu = (int=us);                  // zero-extend unsigned source
    println(String + "wu = " + wu);            // 250

    int big = 321;
    int8 trunc = (int8=big);            // 321 mod 256 -> 65
    println(String + "trunc = " + trunc);      // 65

    int code = 66;
    char cb = (char=code);              // runtime int -> char
    println(String + "cb = " + cb);            // B

    /* ---- pointer source: bool (non-null test) + intptr ---- */

    int^ nul = nullptr;
    bool isn = (bool=nul);              // null -> false
    println(String + "isn = " + isn);          // false

    int v = 9;
    int^ rv = ^v;
    bool nn = (bool=rv);                // non-null -> true
    println(String + "nn = " + nn);            // true

    intptr addr = (intptr=nul);         // null address -> 0
    println(String + "addr = " + addr);        // 0

    /* ---- chaining (right-to-left) ---- */

    float64 chain = (float64 = intptr = nul);   // ptr -> i64 0 -> 0.0
    println(String + "chain = " + chain);              // 0

    bool live = (bool = intptr = rv);           // ptr -> nonzero i64 -> true
    println(String + "live = " + live);                // true

    /* ---- the rest of the value grid (every distinct lowering op) ---- */

    bool fb  = (bool = 2.5);            // float -> bool nonzero test -> true
    println(String + "fb = " + fb);            // true
    bool fb0 = (bool = 0.0);            // float zero -> false
    println(String + "fb0 = " + fb0);          // false

    uint fu = (uint = 2.9);             // float -> unsigned (truncates) -> 2
    println(String + "fu = " + fu);            // 2
    float fzz = 250.7;
    uint8 fu8 = (uint8 = fzz);          // runtime float -> unsigned
    println(String + "fu8 = " + fu8);          // 250

    uint uu = 5;
    float64 uf = (float64 = uu);        // unsigned -> float
    println(String + "uf = " + uf);            // 5
    float64 tf = (float64 = true);      // bool -> float
    println(String + "tf = " + tf);            // 1

    float32 sf = 2.25;
    float64 df = (float64 = sf);        // float32 -> float64 (fpext)
    println(String + "df = " + df);            // 2.25
    float64 wide64 = 3.5;
    float32 df32 = (float32 = wide64);  // float64 -> float32 (fptrunc)
    println(String + "df32 = " + df32);        // 3.5

    /* ---- conversion in a const initializer + an array dimension (fold) ---- */

    const int N = (int = 3.9);          // folds to a strong int constant -> 3
    println(String + "N = " + N);              // 3

    int dim[(int = 2.5)];               // the dimension folds to 2
    dim[0] = 7;
    dim[1] = 8;                         // index 1 is in range iff the dim is 2
    int dsum = dim[0] + dim[1];
    println(String + "dsum = " + dsum);        // 15

    /* ---- wider tiers + same-width reinterpret ---- */

    uint64 u64 = (uint64 = -1);         // reinterpret -1 in 64 bits
    println(String + "u64 = " + u64);          // 18446744073709551615
    int64 n64 = (int64 = -5);           // sign-extended 64-bit
    println(String + "n64 = " + n64);          // -5
    int16 s16 = (int16 = 70000);        // 70000 mod 2^16 -> 4464
    println(String + "s16 = " + s16);          // 4464

    uint uw = 4294967295;
    int iw = (int = uw);                // same-width uint -> int reinterpret
    println(String + "iw = " + iw);            // -1

    /* ---- iterator source (not just a reference) ---- */

    int harr[3];
    harr[0] = 1;
    int[] it = ^harr[0];                // an iterator into the array
    bool itb = (bool = it);             // non-null -> true
    println(String + "itb = " + itb);          // true

    /* ---- signed/unsigned float<->int variant (output-locked, not just emitted) ---- */

    uint8 uhi8 = 200;                   // 0xC8, high bit set
    float64 ufhi = (float64 = uhi8);    // uitofp -> 200 (sitofp would read it as -56)
    println(String + "ufhi = " + ufhi);        // 200

    float64 ufval = 3000000000.0;       // in the uint range, beyond signed int
    uint uconv = (uint = ufval);        // fptoui -> 3000000000 (a value only unsigned holds)
    println(String + "uconv = " + uconv);      // 3000000000

    int negfl = (int = -3.9);           // fold: truncate toward zero -> -3 (not floor -4)
    println(String + "negfl = " + negfl);      // -3
    float nf = -3.9;
    int negfr = (int = nf);             // runtime fptosi toward zero -> -3
    println(String + "negfr = " + negfr);      // -3

    /* ---- char / bool as a runtime conversion source ---- */

    char chs = 'A';
    int chi = (int = chs);              // runtime char (uint8) source -> 65
    println(String + "chi = " + chi);          // 65
    bool bsrc = true;
    int bis = (int = bsrc);             // runtime bool source -> 1
    float64 bfs = (float64 = bsrc);     // runtime bool source -> 1.0
    println(String + "bis = " + bis + " bfs = " + bfs);   // 1 1

    /* tuples — per-slot conversion. */
    (int,int) tpl1 = (65,66);
    tpl2 = ((char,char)=tpl1);
    println(String + "tpl2 = (" + tpl2[0] + "," + tpl2[1] + ")");                   // (A,B)

    /* arrays — per-element conversion. char[3] -> int[3] via the leaf grid. */
    char ch_arr[3] = ('a', 'b', 'c');
    int int_arr[3] = (int[3] = ch_arr);
    println(String + "int_arr = " + int_arr[0] + " " + int_arr[1] + " " + int_arr[2]);  // 97 98 99

    /* a const-EXPRESSION dim baked into the conversion target. */
    const int kC = 3;
    char ch_kc[kC] = ('A', 'B', 'C');
    int int_kc[kC] = (int[kC] = ch_kc);
    println(String + "int_kc = " + int_kc[0] + " " + int_kc[1] + " " + int_kc[2]);  // 65 66 67

    /* nested: tuple-of-tuple. */
    ((int,int),(int,int)) ntpl = ((65,66), (67,68));
    n2 = (((char,char),(char,char))=ntpl);
    println(String + "n2 = ((" + n2[0][0] + "," + n2[0][1] + "),("
              + n2[1][0] + "," + n2[1][1] + "))");                           // ((A,B),(C,D))

    /* nested: tuple-with-array slot. */
    (int, int[2]) tpa = (90, (97, 98));
    tpa2 = ((char, char[2]) = tpa);
    println(String + "tpa2 = (" + tpa2[0] + ", [" + tpa2[1][0] + "," + tpa2[1][1] + "])"); // (Z, [a,b])

    /* multi-dim array. */
    char m2[2][3] = (('a','b','c'), ('d','e','f'));
    int im2[2][3] = (int[2][3] = m2);
    println(String + "im2 = " + im2[0][0] + " " + im2[1][2]);                       // 97 102

    /* array-of-tuple — the symmetric direction to tuple-of-array. The codegen
       array arm peels one outer dim and recurses into the tuple elem. */
    (char,char) at_arr[2] = (('A','B'), ('C','D'));
    aoi = ((int,int)[2] = at_arr);
    println(String + "aoi = (" + aoi[0][0] + "," + aoi[0][1] + ") ("
              + aoi[1][0] + "," + aoi[1][1] + ")");                          // (65,66) (67,68)

    /* CROSS-FORM conversion — an array IS a homogeneous tuple, so the source need
       not match the target's form: a tuple converts to an array target and an array
       to a tuple target (slot count must match; the leaf still uses the explicit
       grid, so per-leaf narrowing is allowed). */
    int xfca[2] = (int[2] = (1, 2));          // tuple literal -> array
    println(String + "xfca = " + xfca[0] + " " + xfca[1]);                          // 1 2
    int xfav[3] = (4, 5, 6);
    xfct = ((int,int,int) = xfav);            // array value -> tuple
    println(String + "xfct = (" + xfct[0] + "," + xfct[1] + "," + xfct[2] + ")");   // (4,5,6)
    (int[2], int[2]) xftoa = ((1,2), (3,4));
    xfaot = ((int,int)[2] = xftoa);           // tuple-of-arrays -> array-of-tuples
    println(String + "xfaot = " + xfaot[0][0] + " " + xfaot[0][1] + " "
              + xfaot[1][0] + " " + xfaot[1][1]);                            // 1 2 3 4
    xfcn = (int8[2] = (300, 2));              // cross-form + explicit per-leaf narrow
    println(String + "xfcn = " + xfcn[0] + " " + xfcn[1]);                          // 44 2

    /* pointer-leaf in a tuple slot (positive): each pointer source converts to
       bool or intptr per the existing leaf rule, applied through the per-slot
       walk. */
    int vp = 42;
    int^ rvp = ^vp;
    int^ nulp = nullptr;
    pirv = ((bool, intptr) = (rvp, nulp));
    println(String + "pirv = " + pirv[0] + " " + pirv[1]);                          // true 0

    /* iterator source in a tuple slot — kIterator is pointer-like, same leaf
       rule. */
    int harr2[3];
    harr2[0] = 1;
    int[] it2 = ^harr2[0];
    bib = ((bool, bool) = (it2, it2));
    println(String + "bib = " + bib[0] + " " + bib[1]);                             // true true

    /* const-EXPRESSION dim baked into a tuple SLOT type (the dim_sink flows
       through the tuple-slot recursion in parseType). */
    (char[kC], char) src_kt = (('A','B','C'), 'Z');
    tup_kt = ((int[kC], int) = src_kt);
    println(String + "tup_kt = (" + tup_kt[0][0] + " " + tup_kt[0][1] + " "
              + tup_kt[0][2] + ", " + tup_kt[1] + ")");                      // (65 66 67, 90)

    /* cross-category in slots — float -> int per slot, exercising the leaf
       grid through the tuple walk. */
    (float, float) ff = (1.5, 2.7);
    cci = ((int, int) = ff);
    println(String + "cci = (" + cci[0] + "," + cci[1] + ")");                      // (1,2)

    /* ---- conversion to a CLASS target — dispatches the class's op= ---- */

    Amt amt1 = (Amt = 500);             // op=(int) fills a temp, copied into amt1
    println(String + "amt1 = " + amt1.cents_); // 500

    Amt amt2 = 0;
    amt2 = (Amt = 250);                 // a conversion assigned to an existing class
    println(String + "amt2 = " + amt2.cents_); // 250

    int cval = 99;
    int^ cp = ^cval;
    Amt amt3 = (Amt = cp);              // pointer source -> op=(int^)
    println(String + "amt3 = " + amt3.cents_); // 99

    /* the conversion temp's scope ENDS AT THE PHRASE: -dtor fires before the next
       statement (a statement-expression position), not at block exit. */
    println(String + "phrase begin");
    trace_use((Trace = 7));             // +ctor 0, op=(7), use 7, -dtor 7 — all here
    println(String + "phrase end");

    /* a CLASS source: op=(Amt^) takes another class by pointer. */
    Amt src = 41;
    Wrap w = (Wrap = src);
    println(String + "w = " + w.w_);           // 41

    /* the temp used IN PLACE — a field read off the conversion result. */
    int inplace = (Amt = 5).cents_;
    println(String + "inplace = " + inplace);  // 5

    /* a conversion at a RETURN position. */
    Amt r = amt_from(88);
    println(String + "r = " + r.cents_);       // 88

    /* TWO conversions in one statement — distinct temps. */
    amt_pair((Amt = 3), (Amt = 4));     // pair = 7

    /* a NAMESPACED member class as the target (single-colon qualified). */
    Money:Cents mc = (Money:Cents = 9);
    println(String + "mc = " + mc.c_);         // 9

    /* an ALIAS to a class as the target. */
    Dollars dol = (Dollars = 6);
    println(String + "dol = " + dol.cents_);   // 6

    /* a VIRTUAL class target — the temp's vptr is stamped, so dispatch works. */
    Vshape vs = (Vshape = 15);
    int va = vs.area();
    println(String + "va = " + va);            // 15

    /* CONDITION position — the temp is scoped to the condition's evaluation
       (lifted through the phrase seq, not the statement-pre path). */
    if ((Amt = 3).cents_ > 0) { println(String + "cond taken"); }

    /* a class LEAF inside an aggregate conversion slot converts PER SLOT, iteratively
       and recursively — each slot dispatches the class op= exactly like a top-level
       '(Class = src)'. */
    (int, int) cls_slot = (1, 2);
    pair_cs = ((Amt, Amt) = cls_slot);
    println(String + "pair_cs = " + pair_cs[0].cents_ + " " + pair_cs[1].cents_);   // 1 2

    /* SAME-CLASS source — no user op= matches an Amt source, so the conversion falls to
       the default whole-value COPY (assignment to a temp), exactly like `Amt x = a`. */
    Amt sc_src; sc_src = 7;
    sc = (Amt = sc_src);
    println(String + "sc = " + sc.cents_);                              // 7

    /* MIXED slots — a class slot and a primitive slot in one tuple target. */
    mx = ((Amt, int) = (30, 40));
    println(String + "mx = " + mx[0].cents_ + " " + mx[1]);             // 30 40

    /* a class ARRAY target — per-ELEMENT op=(int). */
    int csrc[2]; csrc[0] = 5; csrc[1] = 6;
    Amt cav[2] = (Amt[2] = csrc);
    println(String + "cav = " + cav[0].cents_ + " " + cav[1].cents_);   // 5 6

    /* CROSS-FORM with a class element — a tuple literal converts to a class array, and a
       same-class array reshapes to a class tuple (each slot copies). */
    Amt cxf[2] = (Amt[2] = (7, 8));
    println(String + "cxf = " + cxf[0].cents_ + " " + cxf[1].cents_);   // 7 8
    Amt rsh[2]; rsh[0] = 1; rsh[1] = 2;
    rst = ((Amt, Amt) = rsh);
    println(String + "rst = " + rst[0].cents_ + " " + rst[1].cents_);   // 1 2

    /* NESTED aggregate with class leaves at depth — the per-slot walk recurses. */
    nag = (((Amt, int), (int, Amt)) = ((1, 2), (3, 4)));
    println(String + "nag = " + nag[0][0].cents_ + " " + nag[0][1] + " "
              + nag[1][0] + " " + nag[1][1].cents_);             // 1 2 3 4

    /* SPILL — a side-effecting source (a tuple-returning call) is NOT a bare lvalue, so it
       is evaluated ONCE into a temp then indexed per slot (mkpair prints exactly once). */
    sp = ((Amt, Amt) = mkpair());
    println(String + "sp = " + sp[0].cents_ + " " + sp[1].cents_);      // mkpair once; 50 60

    /* CROSS-CLASS in a slot — each slot dispatches Wrap's op=(Amt^) on an Amt source. */
    Amt wsrc[2]; wsrc[0] = 11; wsrc[1] = 22;
    wv = ((Wrap, Wrap) = wsrc);
    println(String + "wv = " + wv[0].w_ + " " + wv[1].w_);              // 11 22

    /* VIRTUAL class in a slot — each slot's temp is vptr-stamped, so dispatch works. */
    vv = ((Vshape, Vshape) = (10, 20));
    println(String + "vv = " + vv[0].area() + " " + vv[1].area());      // 10 20

    /* CHAINED to a class target — right-to-left: (int=3.9) folds to 3, then (Amt=3). */
    chc = (Amt = int = 3.9);
    println(String + "chc = " + chc.cents_);                            // 3

    /* a class WITH A CLASS-TYPED FIELD, same-class copy — the whole value copies
       recursively (the inner Amt field too). */
    Nested nsrc; nsrc = 8;
    nc = (Nested = nsrc);
    println(String + "nc = " + nc.part.cents_ + " " + nc.label_);       // 8 9

    /* FIELD SOURCE — a bare-lvalue aggregate field (`hld.pr_`) converts per slot,
       re-indexed in place with NO spill (contrast mkpair's call source, spilled once). */
    Holder hld( (14, 15) );
    hfld = ((Amt, Amt) = hld.pr_);
    println(String + "hfld = " + hfld[0].cents_ + " " + hfld[1].cents_);   // 14 15

    /* compile errors — each uncommented in isolation by the negative runner. */

    /* a pointer converts only to bool / intptr, not an arbitrary numeric. */
    //-EXPECT-ERROR: a pointer converts only to 'bool' or 'intptr'
    //int^ ep = nullptr;
    //int eq = (int = ep);
    //println(String + "x= " + eq);

    /* the target is never a pointer type. */
    //-EXPECT-ERROR: A type conversion target may not be a pointer type
    //int ev = 5;
    //int^ ey = (int^ = ev);
    //println(String + "x= " + ey^);

    /* a non-value (tuple) source has no scalar conversion. */
    //-EXPECT-ERROR: Cannot convert '(int, int)' to 'int'
    //int et = (int = (1, 2));
    //println(String + "x= " + et);

    /* a non-value target (void) is rejected. */
    //-EXPECT-ERROR: Cannot convert to 'void'; the target must be a value type
    //int es = 5;
    //int ed = (void = es);
    //println(String + "x= " + ed);

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

    /* a NAMED slot in a conversion-TARGET tuple type is "too many names" — the
       target is a nameless type position, so its slots must be anonymous (the name
       belongs on the variable). */
    //-EXPECT-ERROR: A tuple-type slot cannot be named
    //int neg_conv_slot_named() {
    //    (int, int) p = (1, 2);
    //    cs_es = ((int x, int) = p);
    //    return cs_es[0];
    //}

    /* a class conversion whose source matches no op= is rejected — the conversion
       is only as permissive as the class's assignment operators (a float matches
       neither op=(int) nor op=(int^); no implicit narrowing into an operator). */
    //-EXPECT-ERROR: the class has no assignment operator accepting the source
    //int neg_class_no_op() {
    //    float badf = 1.5;
    //    Amt bad = (Amt = badf);
    //    return bad.cents_;
    //}

    /* a conversion target may not be a POINTER — a class pointer rejects like any
       other (the identifier-led trigger routes it to the same check). */
    //-EXPECT-ERROR: A type conversion target may not be a pointer type
    //int neg_class_ptr_target() {
    //    int n = 5;
    //    Amt^ p_es = (Amt^ = n);
    //    return p_es^.cents_;
    //}

    /* converting to an ABSTRACT class default-constructs its temp, so the abstract-
       instantiation check fires. */
    //-EXPECT-ERROR: Cannot instantiate the abstract class
    //int neg_abstract_target() {
    //    ab_es = (Abs = 1);
    //    return ab_es.a_;
    //}

    /* the result is a STRONG typed value, so assigning it to a narrower type is
       rejected just like an int variable would be — it does not flex. */
    //-EXPECT-ERROR: Cannot implicitly narrow 'int' to 'int8'
    //int8  en = (int = 5);
    //println(String + "x= " + en);

    return 0;
}
