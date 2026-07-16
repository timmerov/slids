/*
test condition expressions.

    ! && || ^^ &&= ||= ^^=

the operands must be a condition-expression.
anything that is zero-like (0, 0.0, nullptr, false) is false.
everything else is true.
arrays, tuples, functions returning void are not valid condition expressions.

note:
classes are valid if they override op! (dispatched by `!`); a class without op!
is rejected like any non-coercible type.
*/

/*
claude says:

the operand of `!`, and both operands of `&&` / `||` / `^^`, must coerce to bool
(isCoercibleToBool: numeric or pointer-like — int/uint/float/bool/char, and `^`/`[]`
incl. char[]). a TUPLE or ARRAY operand is rejected: "Operator 'X' is not defined on
type 'Y'." (the negatives below). a void operand is caught EARLIER as "cannot be used
as an expression" (so that arm is unreachable). a CLASS operand dispatches its op!
when defined (Flag in main); a class WITHOUT op! errors the same "not defined" way
as a tuple/array (the neg_not_class negative below).

the SAME coercion governs the if / while / for CONDITIONS (flow/ifelse.sl, while.sl,
forlong.sl) and the logical AUGMENTED-ASSIGN `&&=` / `||=` / `^^=` (same "Operator
'X' is not defined" message). slids has no ternary `?:`.
*/

// A class is a valid condition operand IFF it defines op! — dispatched by `!`.
Flag(int v_ = 0) {
    bool op!() {
        return v_ == 0;
    }
}

int32 main() {

    int x = 5;
    int^ p = ^x;          // non-null reference
    int^ pn = nullptr;    // null reference
    int a[2] = (1, 2);
    int[] it = ^a[0];     // non-null iterator

    // unary `!` on a reference / iterator: null -> true, non-null -> false.
    __println("!p = " + (!p));          // false
    __println("!pn = " + (!pn));        // true
    __println("!it = " + (!it));        // false

    // && / || / ^^ with a pointer-like operand (coerces: non-null true, null false).
    __println("p && true = " + (p && true));      // true
    __println("pn && true = " + (pn && true));    // false
    __println("p || false = " + (p || false));    // true
    __println("pn || false = " + (pn || false));  // false
    __println("p ^^ true = " + (p ^^ true));      // false
    __println("it ^^ false = " + (it ^^ false));  // true

    // logical augmented-assign with a pointer-like rhs.
    bool b1 = false; b1 ||= p;  __println("b1 = " + b1);   // true
    bool b2 = true;  b2 &&= pn; __println("b2 = " + b2);   // false
    bool b3 = false; b3 ^^= it; __println("b3 = " + b3);   // true

    // a CLASS operand with op! — `!` dispatches it (v_ defaults to 0 -> op! true).
    Flag f;
    __println("!flag = " + (!f));   // true

    return 0;
}

/*
negatives — one //-block uncommented per run. a logical operator's operand must
coerce to bool (numeric / pointer-like); a TUPLE or ARRAY operand is rejected. each
operator is tested with a GOOD operand on one side and a BAD one on the other — a
bare `if (badtype)` would exercise the IF-CONDITION check (tested in flow/), not the
operator. a void operand is caught earlier as "cannot be used as an expression".
*/

// unary `!` — its sole operand must coerce.
//-EXPECT-ERROR: Operator '!' is not defined on type '(int, int)'
//int neg_not() {
//    (int, int) t = (1, 2);
//    bool r = !t;
//    __println("" + r);
//    return 0;
//}

// `&&` — a bad operand on either side.
//-EXPECT-ERROR: Operator '&&' is not defined on type '(int, int)'
//int neg_and_rhs() {
//    (int, int) t = (1, 2);
//    bool r = true && t;
//    __println("" + r);
//    return 0;
//}

//-EXPECT-ERROR: Operator '&&' is not defined on type '(int, int)'
//int neg_and_lhs() {
//    (int, int) t = (1, 2);
//    bool r = t && true;
//    __println("" + r);
//    return 0;
//}

// `||` — a bad operand on either side.
//-EXPECT-ERROR: Operator '||' is not defined on type 'int[2]'
//int neg_or_rhs() {
//    int a[2] = (1, 2);
//    bool r = true || a;
//    __println("" + r);
//    return 0;
//}

//-EXPECT-ERROR: Operator '||' is not defined on type 'int[2]'
//int neg_or_lhs() {
//    int a[2] = (1, 2);
//    bool r = a || true;
//    __println("" + r);
//    return 0;
//}

// `^^` — a bad operand on either side.
//-EXPECT-ERROR: Operator '^^' is not defined on type '(int, int)'
//int neg_xor_rhs() {
//    (int, int) t = (1, 2);
//    bool r = true ^^ t;
//    __println("" + r);
//    return 0;
//}

//-EXPECT-ERROR: Operator '^^' is not defined on type '(int, int)'
//int neg_xor_lhs() {
//    (int, int) t = (1, 2);
//    bool r = t ^^ true;
//    __println("" + r);
//    return 0;
//}

// logical augmented-assign — the rhs operand must coerce (lhs is the bool target).
//-EXPECT-ERROR: Operator '&&' is not defined on type '(int, int)'
//int neg_aug_and() {
//    bool b = true;
//    (int, int) t = (1, 2);
//    b &&= t;
//    __println("" + b);
//    return 0;
//}

//-EXPECT-ERROR: Operator '||' is not defined on type 'int[2]'
//int neg_aug_or() {
//    bool b = true;
//    int a[2] = (1, 2);
//    b ||= a;
//    __println("" + b);
//    return 0;
//}

//-EXPECT-ERROR: Operator '^^' is not defined on type '(int, int)'
//int neg_aug_xor() {
//    bool b = true;
//    (int, int) t = (1, 2);
//    b ^^= t;
//    __println("" + b);
//    return 0;
//}

/* a CLASS operand WITHOUT op! is rejected like any type that can't coerce — the
   class must define op! (see Flag in main, which does). */
//-EXPECT-ERROR: Operator '!' is not defined on type 'Box'
//int neg_not_class() {
//    Box(int v_) { }
//    Box b(1);
//    bool r = !b;
//    __println("" + r);
//    return 0;
//}

/* a VOID operand (a `void^` dereference yields void) — rejected for every operator,
   like a tuple/array. (a void CALL is caught earlier as "cannot be used as an
   expression"; a void^ deref is not, so it reaches the operator check.) */
//-EXPECT-ERROR: Operator '!' is not defined on type 'void'
//int neg_not_void() { int x = 5; void^ p = ^x; bool r = !p^; __println("" + r); return 0; }

//-EXPECT-ERROR: Operator '&&' is not defined on type 'void'
//int neg_and_void() { int x = 5; void^ p = ^x; bool r = true && p^; __println("" + r); return 0; }

//-EXPECT-ERROR: Operator '||' is not defined on type 'void'
//int neg_or_void() { int x = 5; void^ p = ^x; bool r = true || p^; __println("" + r); return 0; }

//-EXPECT-ERROR: Operator '^^' is not defined on type 'void'
//int neg_xor_void() { int x = 5; void^ p = ^x; bool r = true ^^ p^; __println("" + r); return 0; }

//-EXPECT-ERROR: Operator '&&' is not defined on type 'void'
//int neg_aug_and_void() { int x = 5; void^ p = ^x; bool b = true; b &&= p^; __println("" + b); return 0; }

//-EXPECT-ERROR: Operator '||' is not defined on type 'void'
//int neg_aug_or_void() { int x = 5; void^ p = ^x; bool b = true; b ||= p^; __println("" + b); return 0; }

//-EXPECT-ERROR: Operator '^^' is not defined on type 'void'
//int neg_aug_xor_void() { int x = 5; void^ p = ^x; bool b = true; b ^^= p^; __println("" + b); return 0; }
