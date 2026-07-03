/*
test overloaded methods.
includes default parameters.
matches the function call to the signature.
applies widening rules.
include infering the type of a default parameter.

ambiguity is a compile error.
*/

/*
claude says:

method / function parity: a method resolves through the SAME overload engine as a
free function. A method name may have several definitions in one class with distinct
signatures; obj.m(args) / self.m(args) / a bare SIBLING m(args) (which rewrites to
self.m()) all rank the candidate set and pick the lowest-total-cost match (exact 0,
widen 1). A tie is "Ambiguous call", no viable candidate is "No matching overload" —
the same wording as a free function.
- DEFAULT PARAMETERS on a method: a trailing param may carry a default; omitted args
  fill from THE CHOSEN overload's defaults (after ranking). A param type may itself be
  INFERRED from its default value (`x = 7` -> int).
- the implicit receiver `_$recv` (params[0]) is held OUT of ranking + the arity range;
  it always matches the object.
- a single (non-overloaded) method keeps its detailed arity / cast errors.
*/

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
        __println("inftypes: " + ##type(a) + " " + ##type(b) + " "
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

    __println("area1 = " + c.area(5));              // 25
    __println("area2 = " + c.area(3, 4));           // 12

    int32 i = 7;
    float32 f = 2.5;
    __println("kind_i = " + c.kind(i));             // 1
    __println("kind_f = " + c.kind(f));             // 2

    int64 j = 9;
    __println("rank_i = " + c.rank(i));             // 32 (exact)
    __println("rank_j = " + c.rank(j));             // 64 (exact)

    __println("withdef1 = " + c.withdef(5));        // 105 (int overload, b=100, base_=0)
    __println("withdef2 = " + c.withdef(5, 6));     // 11
    __println("withdef_f = " + c.withdef(2.5));     // 999 (float overload)

    __println("inf0 = " + c.inf());                 // 7
    __println("inf1 = " + c.inf(5));                // 5
    c.inftypes();                                   // inftypes: int float char int64
    __println("finf0 = " + c.finf());               // 3.5 (float: 2.5 + 1.0)
    __println("finf1 = " + c.finf(4.5));            // 5.5

    __println("viaself = " + c.viaself(6));         // 36
    __println("viabare = " + c.viabare(3, 4));      // 12

    // overload resolution through a DEREF receiver (`p^.method`).
    Calc^ p = ^c;
    __println("ptr_area1 = " + p^.area(5));         // 25
    __println("ptr_area2 = " + p^.area(3, 4));      // 12

    // a single method with multiple defaults — fill left to right.
    __println("multi1 = " + c.multi(1));            // 6  (1+2+3)
    __println("multi2 = " + c.multi(1, 20));        // 24 (1+20+3)
    __println("multi3 = " + c.multi(1, 20, 30));    // 51 (1+20+30)

    // overload on a class-typed param vs a primitive.
    __println("g_int = " + c.g(5));                 // 1
    __println("g_ref = " + c.g(^c));                // 2

    return 0;
}

/* a call where two overloads are equally good (int16 widens to both int32 and
   int64 — both cost 1) is ambiguous. */
//-EXPECT-ERROR: Ambiguous call to 'rank'
//int32 amb() {
//    Calc c(0);
//    int16 s = 3;
//    return c.rank(s);
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

/* a default parameter makes two overloads equally viable for the same call. */
//-EXPECT-ERROR: Ambiguous call to 'amb_one'
//Two(int n_) {
//    int amb_one(int32 x) { return 1; }
//    int amb_one(int32 x, int32 y = 0) { return 2; }
//}
//int32 amb_call() {
//    Two t(0);
//    return t.amb_one(5);
//}

/* a SINGLE (non-overloaded) method keeps its detailed arity error — a RANGE when it
   carries defaults (multi takes 1..3 args). */
//-EXPECT-ERROR: Method 'multi' expects 1 to 3 arguments, got 4
//int32 neg_arity_range() {
//    Calc c(0);
//    return c.multi(1, 2, 3, 4);
//}

/* a BARE sibling call (rewrites to self.r) is overload-resolved too — int16 widens
   to both r(int32) and r(int64), so it is ambiguous. */
//-EXPECT-ERROR: Ambiguous call to 'r'
//Sib(int n_) {
//    int r(int32 x) { return 1; }
//    int r(int64 x) { return 2; }
//    int pick(int16 s) { return r(s); }
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
