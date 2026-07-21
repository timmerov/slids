/*
test overloaded functions.
includes default parameters.

the function call is matched to the function signatures.
the outcomes are: no match, match, ambiguous.
no match and ambiguity are compile errors.

matching is done by stage.
matching criteria is relaxed each stage.
every parameter must meet the matching criteria.

stages:
1. exact match.
2. alias
3. a single implicit pointer cast.
4. smallest same-sign widening.
5. smallest cross-sign widening.

    alias Integer = int;
    void fn(int a);         // A
    void fn(Integer a);     // B
    void fn(int64 a);       // C
    void fn(uint16 a);      // D

    int x;      fn(x);      // A
    Integer y;  fn(y);      // B
    int64 z;    fn(z);      // C
    uint w;     fn(w);      // C
    uint8 u;    fn(u);      // D
    fn(1);                  // A
    fn(5_000_000_000);      // C

    Base(int a) { }
    Base : Derived() { }
    Derived : Super() { }

    void fn(Base^ p);       // E
    void fn(Super^ p);      // F
    void fn(intptr i);      // G

    Base^ bp;     fn(bp);   // E
    Derived^ dp;  fn(dp);   // E,G ambiguous
    Super^ sp;    fn(sp);   // F
    void^ vp;     fn(vp);   // G

    void fn(int^ p);        // H
    void fn(int[5]^ p);     // J

    int arr[5];
    fn(arr);                // J
    fn(^arr);               // J
    fn(^arr[0]);            // H

    void fn(char^ p);       // K

    char str[5];
    fn(str);                // K
    fn(^str);               // K
    fn(^str[0]);            // K

    void fn(int a, int b = 0);  // L
    void fn(int64 a);           // M
    int i;      fn(i);          // L
    int64 big;  fn(big);        // M

ambiguous cases:

    void fn(int a, int64 b);
    void fn(int64 a, int b);
    int x; fn(x, x);

possible no match case:
current implementation is this is a single cast.
subject to review in the future.

    void fn(int^ p);
    int arr[5]; fn(arr);
        --> fn(^arr) for free
        --> fn(^arr[0]) implicit cast to int[].
        --> fn(<int^> ^arr[0]) second implicit cast.

mutable pointer exact matches const pointer of the same type.
this rule recurses through the pointer type down to the leaf.
for example: argument type int^ is an exact match to parameter type
const (const int)^.

the convenience convention plays no part in matching.
ie syntax is pass-by-value but the semantics are pass-by-reference.

    fn(obj);  -->  fn(^obj);

consider this matching problem:

    void fn(int a);
    void fn(int a, int b = 0);

the reason is fn(i) is either ambiguous: it matches both fn(a) and fn(a,0) exactly.
in which case, we can never call fn(a).
or it matches fn(a).
in which case, we can never call fn(a,b) with the default value for b.
therefore this is a compile error.
*/

/*
claude says:

- a name may have several function definitions with distinct signatures. A call
  matches the candidate set: each arg must convert to the param (a narrowing /
  cross-family rejects the candidate).
- ranking is a per-arg RUNG on a ladder (argConvertCost), lowest = tightest:
    0 exact  1 alias  2 a single implicit pointer cast  3/4/5 smallest same-sign
    widening (16/32/64-bit target)  6/7/8 smallest cross-sign widening.
  A candidate's score is the MAX rung over its args (rankOverload — the worst
  parameter); the lowest score wins, a tie is "Ambiguous call", no viable
  candidate is "No matching overload". So widening is graded, not flat: an int16
  arg picks int32 (same32) over int64 (same64); a uint8 arg picks uint16
  (same-sign) over int64 (cross-sign). The pointer-cast rung is FLAT — a derived
  pointer matching both a base-pointer overload (demotion) and an intptr overload
  (strip) ties (amb_ptr). `max` not `sum`: two mirror-image candidates that each
  match one arg exactly and widen the other tie (amb_sym). (A literal's exactness
  is judged against its default type; the whole-array `^arr` and value->reference
  convenience are exact = rung 0.)
- DEFAULT PARAMETERS: a trailing param may carry a default (`int32 b = 100`), so a
  candidate's arity is a RANGE — num_required .. param-count. A call within the
  range is viable; the omitted trailing args fill from THE CHOSEN overload's
  defaults (fillDefaults, AFTER ranking). The param type may itself be INFERRED
  from the default value (`x = 5` -> int), like an inferred local — a typeless
  param with no default has nothing to infer from and is an error.
- THE OVERLOAD SET IS CHECKED WHERE IT IS DECLARED, not at a call: two overloads
  whose arity RANGES overlap at some arity n, and whose first n parameter types are
  identical, can never be told apart by a call with n arguments — so the PAIR is the
  error ("Ambiguous overloads of 'fn'", the amb_one / amb_zero negatives). `fn(int)`
  + `fn(int, int = 0)` is the canon case: either fn(i) is ambiguous (fn(int) becomes
  uncallable) or it picks fn(int) (b's default becomes unreachable) — both readings
  are broken, so neither is taken. Only a default can make two ranges overlap; with
  none, an overlap means identical signatures, which is a duplicate definition. The
  overloads still stand when the prefix DIFFERS at the shared arity — `lm(int,
  int = 0)` beside `lm(int64)` distinguishes at arity 1, so both are callable.
- single-overload names keep their existing detailed errors (arity / narrow).
*/

/* arity overload — same name, different parameter counts. */
int32 area(int32 s) {
    return s * s;
}
int32 area(int32 w, int32 h) {
    return w * h;
}

/* type overload — same arity, different parameter types. */
int32 kind(int32 x) {
    return 1;
}
int32 kind(float32 x) {
    return 2;
}

/* exact match wins over a widening alternative. */
int32 rank(int32 x) {
    return 32;
}
int32 rank(int64 x) {
    return 64;
}

/* one viable candidate via widening (the other is cross-family, rejected). */
int32 conv(int32 x) {
    return 1;
}
int32 conv(float64 x) {
    return 2;
}

/* same-sign widening beats cross-sign: a uint8 arg widens SAME-SIGN to uint16
   (rung same16) and CROSS-SIGN to int64 (rung diff64); the same-sign target wins. */
int32 sign(uint16 x) {
    return 16;
}
int32 sign(int64 x) {
    return 64;
}

/* alias distinction: a call prefers the EXACT-name overload — an int32 arg picks
   the int32 overload (exact) over the alias one, an Integer arg the reverse. */
alias Integer = int32;
int32 aov(int32 x) {
    return 100;
}
int32 aov(Integer x) {
    return 200;
}

/* a 3-level hierarchy (Super <: Derived <: Base) with base-pointer, super-pointer,
   and intptr overloads. A base/super pointer matches its own overload exactly; a
   void pointer only reaches intptr; a DERIVED pointer matches the base overload via
   a demotion AND intptr via a strip — same cast rung, so ambiguous (amb_ptr). */
Base(int a) { }
Base : Derived() { }
Derived : Super() { }
int32 pk(Base^ p) {
    return 1;
}
int32 pk(Super^ p) {
    return 3;
}
int32 pk(intptr n) {
    return 2;
}

/* array whole-ref vs element decay: a bare array matches the whole-array-ref param
   (int[5]^, via the `^arr` convenience, EXACT) over the element-ref param (int^, an
   element-decay cast) — so it is no longer ambiguous, the whole-ref wins. */
int32 arrp(int^ p) {
    return 1;
}
int32 arrp(int[5]^ p) {
    return 5;
}

/* cross-sign widening dispatches when it is the only viable target: a uint32 arg
   widens cross-sign to int64 (value-preserving); uint16 would narrow (rejected). */
int32 xs(int64 x) {
    return 64;
}
int32 xs(uint16 x) {
    return 16;
}

/* MAX not SUM: mirror candidates — one exact on arg0 + a CAST on arg1, the other a
   WIDEN on arg0 + exact on arg1. The cast-worst candidate (max rung 2) beats the
   widen-worst (max rung 5): a clear winner, not a tie. */
int32 mx(int a, intptr b) {
    return 1;
}
int32 mx(int64 a, int^ b) {
    return 2;
}

/* smallest same-sign widening among floats (float32 -> float64); the int overload
   is cross-family and rejected. */
int32 fw(float64 x) {
    return 64;
}
int32 fw(int32 x) {
    return 32;
}

/* an ALIAS arg that also widens: Small is an alias of int16; the alias is
   transparent under the widen, so it grades by the underlying (int16 -> int32
   same-sign, the smallest, beats int64). */
alias Small = int16;
int32 awiden(int32 x) {
    return 320;
}
int32 awiden(int64 x) {
    return 640;
}

/* aggregate value -> reference with PER-LEAF widening (leafWidenRung): an
   (int16,int16) value widens per leaf to (int32,int32) (max leaf rung same32) —
   tighter than to (int64,int64) (same64), so the int32 tuple overload wins. */
int32 agg((int32,int32)^ p) {
    return 32;
}
int32 agg((int64,int64)^ p) {
    return 64;
}

/* default-param arity range vs a widening alternative: an int arg matches the
   defaulted overload EXACTLY at arity 1 (beats the int64 widen); an int64 arg
   narrows the defaulted one (rejected) and matches int64 exactly. */
int32 lm(int a, int b = 0) {
    return 1;
}
int32 lm(int64 a) {
    return 2;
}

/* a char array decays to a char^ element-reference param, like the int arrays. */
int32 chp(char^ p) {
    return 7;
}

/* nullptr flexes into a pointer param. */
int32 np(int^ p) {
    return 9;
}

/* an overload carrying a default parameter beside a cross-family alternative;
   omitted trailing args fill from the default on the chosen overload. */
int32 withdef(int32 a, int32 b = 100) {
    return a + b;
}
int32 withdef(float32 a) {
    return 999;
}

/* parameter mismatch */
void intparam(int x) { }

int32 main() {
    __println("area(5) = " + area(5));              // 25
    __println("area(3, 4) = " + area(3, 4));        // 12

    int32 i = 7;
    float32 f = 2.5;
    __println("kind(i32) = " + kind(i));            // 1
    __println("kind(f32) = " + kind(f));            // 2

    int64 j = 9;
    int16 s = 3;
    __println("rank(i32) = " + rank(i));            // 32 (exact)
    __println("rank(i64) = " + rank(j));            // 64 (exact)
    __println("rank(i16) = " + rank(s));            // 32 (smallest widening int16->int32)

    uint8 u = 5;
    __println("sign(u8) = " + sign(u));             // 16 (uint8->uint16 same-sign beats ->int64)

    __println("conv(i16) = " + conv(s));            // 1 (int16 widens to int32)

    __println("withdef(5) = " + withdef(5));        // 105 (int overload, b=100)
    __println("withdef(5, 6) = " + withdef(5, 6));  // 11
    __println("withdef(2.5) = " + withdef(2.5));    // 999 (float overload)

    __println("rank(big) = " + rank(5_000_000_000)); // 64 (literal defaults to int64, exact)

    Integer iv = 7;
    __println("aov(int) = " + aov(i));              // 100 (exact int32)
    __println("aov(alias) = " + aov(iv));           // 200 (exact Integer)

    uint w2 = 5;
    __println("xs(u32) = " + xs(w2));               // 64 (uint32->int64 cross-sign; uint16 narrows)

    float32 fv = 1.5;
    __println("fw(f32) = " + fw(fv));               // 64 (float32->float64 widen; int rejected)

    Small sm = 3;
    __println("awiden(alias) = " + awiden(sm));     // 320 (int16 alias widens same-sign to int32)

    (int16, int16) tv = (3, 4);
    __println("agg(tuple) = " + agg(tv));           // 32 (per-leaf widen to int32 beats int64)

    __println("lm(i32) = " + lm(i));                // 1 (exact at arity 1 beats int64 widen)
    __println("lm(i64) = " + lm(j));                // 2 (int64 exact; L narrows, rejected)

    int mv = 3;
    __println("mx(asym) = " + mx(mv, ^mv));         // 1 (cast-worst 2 beats widen-worst 5)

    int aw[5];
    aw[0] = 11;
    __println("arrp(arr) = " + arrp(aw));           // 5 (whole-array ref exact beats element decay)
    __println("arrp(elem) = " + arrp(^aw[0]));      // 1 (int^ via one iter->ref cast)

    char cstr[5];
    cstr[0] = 'x';
    __println("chp(arr) = " + chp(cstr));           // 7 (char array element decay to char^)

    __println("np(null) = " + np(nullptr));         // 9 (nullptr flexes into int^)

    Base b(1);
    Base^ bp = ^b;
    __println("pk(base) = " + pk(bp));              // 1 (Base^ exact)
    Super sup(1);
    Super^ spp = ^sup;
    __println("pk(super) = " + pk(spp));            // 3 (Super^ exact)
    void^ vpp = ^b;
    __println("pk(void) = " + pk(vpp));             // 2 (void^->intptr; typed ptrs rejected)

    /* a string literal is `const char[N]` — storage, N counting the NUL — so the
       rejection now names the literal's real type and its size. */
    //-EXPECT-ERROR: Cannot assign 'const char[14]' to 'int'
    //intparam("passed string");

    return 0;
}

/* two overloads a call is equally good for: `int` matches one arg exactly and
   widens same-sign to int64 on the other, mirror images — neither candidate is
   no-worse on every arg, so the call is ambiguous (the max-rung tie). */
//-EXPECT-ERROR: Ambiguous call to 'sym'
//void sym(int a, int64 b) { }
//void sym(int64 a, int b) { }
//int32 amb_sym() {
//    int x = 3;
//    sym(x, x);
//    return 0;
//}

/* a DERIVED pointer matches both the base-pointer overload (a derived->base
   demotion) and the intptr overload (a strip) at the same cast rung -> ambiguous. */
//-EXPECT-ERROR: Ambiguous call to 'pk'
//int32 amb_ptr() {
//    Derived dd(1);
//    Derived^ ddp = ^dd;
//    return pk(ddp);
//}

/* nearest-base is NOT preferred (the cast rung is flat): a Super pointer matches
   both nb(Derived^) (a 1-hop demotion) and nb(Base^) (a 2-hop demotion) at the same
   rung -> ambiguous. */
//-EXPECT-ERROR: Ambiguous call to 'nb'
//int32 nb(Base^ p) { return 1; }
//int32 nb(Derived^ p) { return 2; }
//int32 amb_nearest() {
//    Super sn(1);
//    Super^ snp = ^sn;
//    return nb(snp);
//}

/* a call matching no overload's arity. */
//-EXPECT-ERROR: No matching overload for 'area'
//int32 nomatch() {
//    return area(1, 2, 3);
//}

/* a call whose argument converts to no overload's parameter (float64 cannot
   narrow to float32 nor cross to int32). */
//-EXPECT-ERROR: No matching overload for 'kind'
//int32 nomatch_type() {
//    float64 d = 1.0;
//    return kind(d);
//}

/* THE DECLARATION IS THE ERROR (no call needed): a default parameter makes the two
   overloads' arity ranges overlap at 1 argument, where their prefixes are identical
   (int32) — so a 1-arg call could never pick between them. */
//-EXPECT-ERROR: Ambiguous overloads of 'amb_one': a call with 1 argument matches both.
//int32 amb_one(int32 x) {
//    return 1;
//}
//int32 amb_one(int32 x, int32 y = 0) {
//    return 2;
//}

/* the same rule at arity ZERO: an all-default overload collides with the nullary one
   (both admit a 0-argument call, with an empty — hence identical — prefix). */
//-EXPECT-ERROR: Ambiguous overloads of 'amb_zero': a call with 0 arguments matches both.
//int32 amb_zero() {
//    return 1;
//}
//int32 amb_zero(int32 x = 0) {
//    return 2;
//}
