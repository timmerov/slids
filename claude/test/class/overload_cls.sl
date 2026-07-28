/*
test overloaded methods.
include infering the type of a default parameter.

usess the same rules as overloading bare functions.
*/

/*
claude says:

method / function parity: a method resolves through the SAME overload engine as a
free function. A method name may have several definitions in one class with distinct
signatures; obj.m(args) / self.m(args) / a bare SIBLING m(args) (which rewrites to
self.m()) all rank the candidate set on the per-arg rung ladder and pick the lowest
MAX-rung match (see overload_fn.sl for the ladder: exact 0, alias 1, cast 2, then
smallest same-sign / cross-sign widening). A tie is "Ambiguous call", no viable
candidate is "No matching overload" — the same wording as a free function.
- DEFAULT PARAMETERS on a method: a trailing param may carry a default; omitted args
  fill from THE CHOSEN overload's defaults (after ranking). A param type may itself be
  INFERRED from its default value (`x = 7` -> int).
- and, as for a free function, the SET is checked WHERE IT IS DECLARED: two method
  overloads whose arity ranges overlap at some arity n with identical first n param
  types are rejected right there ("Ambiguous overloads of 'am1'", the amb_d negatives),
  because no n-argument call could ever tell them apart. The receiver is in both
  prefixes and cancels out, so the rule reads exactly as the free-function one.
  `withdef(int, int = 100)` beside `withdef(float32)` still stands — they differ at
  arity 1.
- the implicit receiver `_$recv` (params[0]) is held OUT of ranking + the arity range;
  it always matches the object.
- a single (non-overloaded) method keeps its detailed arity / cast errors.
*/

import string;

Calc(int base_) {
    // arity overload
    int area(int s) { return s * s; }
    int area(int w, int h) { return w * h; }

    // type overload
    int kind(int32 x) { return 1; }
    int kind(float32 x) { return 2; }

    // exact match wins over a widening alternative
    int rank(int32 x) { return 32; }
    int rank(int64 x) { return 64; }

    // a default parameter beside a cross-family alternative; omitted trailing args
    // fill from the chosen overload's default
    int withdef(int a, int b = 100) { return a + b + base_; }
    int withdef(float32 a) { return 999; }

    // a param whose type is inferred from its default value
    int inf(x = 7) { return x; }

    // each param's TYPE is inferred from its default literal — int / float / char,
    // and the WIDTH follows the value (9999999999 exceeds int32 -> int64). ##type
    // shows the inferred type directly.
    void inftypes(a = 7, b = 1.5, c = 'q', d = 9999999999) {
        println(String + "inftypes: " + ##type(a) + " " + ##type(b) + " "
                  + ##type(c) + " " + ##type(d));
    }

    // an inferred float param behaves as a float (float arithmetic, not int).
    float finf(x = 2.5) { return x + 1.0; }

    // overload resolution reached via `self.` and via a bare SIBLING call
    int viaself(int x) { return self.area(x); }
    int viabare(int x, int y) { return area(x, y); }

    // a single (non-overloaded) method with MULTIPLE defaults — omitted trailing
    // args fill left to right from the defaults.
    int multi(int a, int b = 2, int c = 3) { return a + b + c; }

    // overloading on a CLASS-typed (by-reference) param vs a primitive.
    int g(int x) { return 1; }
    int g(Calc^ x) { return 2; }
}

int32 main() {

    Calc c(0);

    println(String + "area1 = " + c.area(5));              // 25
    println(String + "area2 = " + c.area(3, 4));           // 12

    int32 i = 7;
    float32 f = 2.5;
    println(String + "kind_i = " + c.kind(i));             // 1
    println(String + "kind_f = " + c.kind(f));             // 2

    int64 j = 9;
    println(String + "rank_i = " + c.rank(i));             // 32 (exact)
    println(String + "rank_j = " + c.rank(j));             // 64 (exact)
    int16 s16 = 3;
    println(String + "rank_s = " + c.rank(s16));           // 32 (smallest widening int16->int32)

    println(String + "withdef1 = " + c.withdef(5));        // 105 (int overload, b=100, base_=0)
    println(String + "withdef2 = " + c.withdef(5, 6));     // 11
    println(String + "withdef_f = " + c.withdef(2.5));     // 999 (float overload)

    println(String + "inf0 = " + c.inf());                 // 7
    println(String + "inf1 = " + c.inf(5));                // 5
    c.inftypes();                                   // inftypes: int float char int64
    println(String + "finf0 = " + c.finf());               // 3.5 (float: 2.5 + 1.0)
    println(String + "finf1 = " + c.finf(4.5));            // 5.5

    println(String + "viaself = " + c.viaself(6));         // 36
    println(String + "viabare = " + c.viabare(3, 4));      // 12

    // overload resolution through a DEREF receiver (`p^.method`).
    Calc^ p = ^c;
    println(String + "ptr_area1 = " + p^.area(5));         // 25
    println(String + "ptr_area2 = " + p^.area(3, 4));      // 12

    // a single method with multiple defaults — fill left to right.
    println(String + "multi1 = " + c.multi(1));            // 6  (1+2+3)
    println(String + "multi2 = " + c.multi(1, 20));        // 24 (1+20+3)
    println(String + "multi3 = " + c.multi(1, 20, 30));    // 51 (1+20+30)

    // overload on a class-typed param vs a primitive.
    println(String + "g_int = " + c.g(5));                 // 1
    println(String + "g_ref = " + c.g(^c));                // 2

    return 0;
}

/* two method overloads a call is equally good for: mirror images that each match
   one arg exactly and widen the other same-sign to int64 — neither dominates, so
   the max-rung score ties -> ambiguous. */
//-EXPECT-ERROR: Ambiguous call to 'mm'
//AmbM(int n_) {
//    int mm(int a, int64 b) { return 1; }
//    int mm(int64 a, int b) { return 2; }
//}
//int32 amb_sym() {
//    AmbM a(0);
//    int x = 3;
//    return a.mm(x, x);
//}

/* a call matching no overload's arity. */
//-EXPECT-ERROR: No matching overload for 'area'
//int32 nomatch() {
//    Calc c(0);
//    return c.area(1, 2, 3);
//}

/* a call whose argument converts to no overload's parameter (float64 cannot
   narrow to float32 nor cross to int32). */
//-EXPECT-ERROR: No matching overload for 'kind'
//int32 nomatch_type() {
//    Calc c(0);
//    float64 d = 1.0;
//    return c.kind(d);
//}

/* THE DECLARATION IS THE ERROR (no call needed) — the same rule as a free function
   (overload_fn.sl amb_one): a default makes the two methods' arity ranges overlap at
   1 argument, where their prefixes are identical (int32), so a 1-arg call could never
   pick between them. The implicit receiver sits in both prefixes and cancels out. */
//-EXPECT-ERROR: Ambiguous overloads of 'am1': a call with 1 argument matches both.
//AmbD1(int n_) {
//    int am1(int32 x) { return 1; }
//    int am1(int32 x, int32 y = 0) { return 2; }
//}

/* the same rule at arity ZERO (overload_fn.sl amb_zero): an all-default method
   collides with the nullary one. */
//-EXPECT-ERROR: Ambiguous overloads of 'am0': a call with 0 arguments matches both.
//AmbD0(int n_) {
//    int am0() { return 1; }
//    int am0(int32 x = 0) { return 2; }
//}

/* a SINGLE (non-overloaded) method keeps its detailed arity error — a RANGE when it
   carries defaults (multi takes 1..3 args). */
//-EXPECT-ERROR: Method 'multi' expects 1 to 3 arguments, got 4
//int32 neg_arity_range() {
//    Calc c(0);
//    return c.multi(1, 2, 3, 4);
//}

/* a BARE sibling call (rewrites to self.r) is overload-resolved too — mirror-image
   overloads that each match one arg exactly and widen the other are ambiguous. */
//-EXPECT-ERROR: Ambiguous call to 'r'
//Sib(int n_) {
//    int r(int a, int64 b) { return 1; }
//    int r(int64 a, int b) { return 2; }
//    int pick(int s) { return r(s, s); }
//}

/* a typeless method param with NO default has nothing to infer from. */
//-EXPECT-ERROR: needs an explicit type or a default value
//Bad1(int n_) {
//    int f(x) { return x; }
//}

/* a required method param may not follow an optional (defaulted) one. */
//-EXPECT-ERROR: A required parameter cannot follow an optional parameter
//Bad2(int n_) {
//    int f(int a = 1, int b) { return a + b; }
//}

/* two methods with IDENTICAL signatures are a duplicate definition, not an
   overload. */
//-EXPECT-ERROR: Duplicate definition of 'f'
//Bad3(int n_) {
//    int f(int x) { return 1; }
//    int f(int x) { return 2; }
//}

/* a method default value that does not fit its declared param type. */
//-EXPECT-ERROR: Default value does not fit parameter type 'int8'
//Bad4(int n_) {
//    int f(int8 x = 9999) { return x; }
//}
