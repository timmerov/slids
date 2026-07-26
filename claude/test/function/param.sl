/*
test type inference of function parameters
using the default value type.

default value is a constant expression.

parameters with no default value must have an explicit type.
parameters with no explicit type must have a default value.

parameters with no default value are required.
parameters with a default value are optional.
required parameters first, followed by optional parameters.

    void foo(int a, int b = 0, c = 1) {
        __println(a + b + c);
    }

non-primitive parameters are passed by pointer.
for convenience, call by value syntax is accepted.
under the hood, it's pass by pointer.

note:

technically, non-primitive parameters are passed by pointer to const.
unless explicitly marked as mutable.
but const correctness hasn't landed yet.

*/

/*
claude says:

- a param is required (explicit type, no default) or optional (has a default);
  a typeless param infers its type from the default; required precede optional.
- A PARAMETER DEFAULT IS DATA (2026-07-26 — the globals/fields rule, the same
  isConstantInit predicate): a foldable constant, a string literal, nullptr.
  A call, a construction, or a global read in a default is code in the fill
  path — rejected at the definition ("pass the value at the call site").
  The validated default is kept as a NODE on the entry (the field_params
  model, no more text flattening) and CLONED per call site by fillDefaults,
  restamped to the call's location; a rejected default's slot stays null and
  never fills (this killed the inferExpr assert the old sentinel hit when a
  call omitted a rejected default). Arity is the range [required, total].
*/

int one() {
    return 1;
}

/* mixed: explicit-typed default (b) + typeless default inferred int (c). */
int sum3(int a, int b = 10, c = 100) {
    return a + b + c;
}

/* a constant-expression default (2 * 3 folds to 6), typeless -> inferred int. */
int with_expr(int a, b = 2 * 3) {
    return a + b;
}

/* a void function with a typeless default. */
void announce(n = 42) {
    __println("n=" + n);
}

/* a typeless char default -> the param infers char. */
int code(ch = 'z') {
    return ch;
}

/* a STRING-LITERAL default — const char[N] storage, data through and through
   (the isConstantInit string arm, shared with fields). */
int lead(char[] s = "hi") {
    return s[0];
}

/* class as parameter */
Class(int x_) {
}
void classparam(Class^ ref) {
    nestedclassparam(ref);
    void nestedclassparam(Class^ ref) {
        __println("called nested function with class parameter: " + ref^.x_);
    }
}

int32 main() {
    __println("sum3(1) = " + sum3(1));              // 111
    __println("sum3(1, 2) = " + sum3(1, 2));        // 103
    __println("sum3(1, 2, 3) = " + sum3(1, 2, 3));  // 6
    __println("with_expr(10) = " + with_expr(10));          // 16
    __println("with_expr(10, 20) = " + with_expr(10, 20));  // 30
    announce();                                     // n=42
    announce(7);                                    // n=7
    __println("code() = " + code());                // 122
    __println("lead() = " + lead());                // 104 ('h')
    __println("lead(\"za\") = " + lead("za"));      // 122 ('z')

    Class cls(42);
    classparam(^cls);
    cls.x_ = 37;
    classparam(cls);

    return 0;
}

/* a parameter with no type and no default has nothing to infer from. */
//-EXPECT-ERROR: needs an explicit type or a default value
//void neg_no_type(a) {
//    __println("x");
//}

/* a required parameter cannot follow an optional one. */
//-EXPECT-ERROR: A required parameter cannot follow an optional parameter.
//int neg_order(int a = 0, int b) {
//    return a + b;
//}

/* a default that overflows the explicit parameter type. */
//-EXPECT-ERROR: Default value does not fit parameter type 'int8'.
//int neg_fit(int8 b = 300) {
//    return b;
//}

/* a non-constant default (a function call): a default is DATA — the
   globals/fields rule, one predicate. */
//-EXPECT-ERROR: is not a constant expression
//int neg_const(int a, b = one()) {
//    return a + b;
//}

/* ...and a CALL SITE that OMITS the rejected default (was the inferExpr
   ASSERT: the old text-capture left a sentinel that fillDefaults happily
   materialized — the compiler core-dumped before the real diagnostic
   printed; the slot now stays null and never fills). */
//-EXPECT-ERROR: is not a constant expression
//int neg_const_call(int a = one()) {
//    return a;
//}
//int neg_const_use() {
//    return neg_const_call();
//}

/* a default that reads a GLOBAL is code in the fill path, not data — the
   same policy that governs a global initializer reading another global. */
//-EXPECT-ERROR: is not a constant expression
//global int g_neg = 3;
//int neg_global(int a = g_neg) {
//    return a;
//}

/* a non-primitive VALUE parameter is rejected: primitives pass by value, everything
   else (tuple, class) must be a pointer (reference / iterator) or an array. */
//-EXPECT-ERROR: A non-primitive parameter must be a pointer
//int neg_tuple_val( (int, int) t ) {
//    return t[0] + t[1];
//}

/* the same rule for a class value parameter. */
//-EXPECT-ERROR: A non-primitive parameter must be a pointer
//int neg_class_val( Class c ) {
//    return c.x_;
//}

