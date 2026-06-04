/*
test overloaded functions.
includes default parameters.
matches the function call to the signature.
applies widening rules.

ambiguity is a compile error.
*/

/*
claude says:

(this round: NO default parameters — fixed-arity overloads only.)
- a name may have several function definitions with distinct signatures. A call
  matches the candidate set: each arg must convert to the param (exact or a
  widening within family; a narrowing / cross-family rejects the candidate).
- ranking: per arg, exact = cost 0, widening = cost 1; the lowest-total-cost
  candidate wins; a tie is "Ambiguous call". No viable candidate is "No matching
  overload". (A literal's exactness is judged against its default type.)
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

/* an overload carrying a default parameter beside a cross-family alternative;
   omitted trailing args fill from the default on the chosen overload. */
int32 withdef(int32 a, int32 b = 100) {
    return a + b;
}
int32 withdef(float32 a) {
    return 999;
}

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

    __println("conv(i16) = " + conv(s));            // 1 (int16 widens to int32)

    __println("withdef(5) = " + withdef(5));        // 105 (int overload, b=100)
    __println("withdef(5, 6) = " + withdef(5, 6));  // 11
    __println("withdef(2.5) = " + withdef(2.5));    // 999 (float overload)
    return 0;
}

/* a call where two overloads are equally good (int16 widens to both int32 and
   int64 — both cost 1) is ambiguous. */
//-EXPECT-ERROR: Ambiguous call to 'rank'
//int32 amb() {
//    int16 s = 3;
//    return rank(s);
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

/* a default parameter makes two overloads equally viable for the same call. */
//-EXPECT-ERROR: Ambiguous call to 'amb_one'
//int32 amb_one(int32 x) {
//    return 1;
//}
//int32 amb_one(int32 x, int32 y = 0) {
//    return 2;
//}
//int32 amb_call() {
//    return amb_one(5);
//}
