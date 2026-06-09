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

*/

/*
claude says:

- a param is required (explicit type, no default) or optional (has a default);
  a typeless param infers its type from the default; required precede optional.
- the default is a constant expression (folded); an omitted trailing arg at a
  call site is filled with it. Arity is the range [required, total].
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

int32 main() {
    __println("sum3(1) = " + sum3(1));              // 111
    __println("sum3(1, 2) = " + sum3(1, 2));        // 103
    __println("sum3(1, 2, 3) = " + sum3(1, 2, 3));  // 6
    __println("with_expr(10) = " + with_expr(10));          // 16
    __println("with_expr(10, 20) = " + with_expr(10, 20));  // 30
    announce();                                     // n=42
    announce(7);                                    // n=7
    __println("code() = " + code());                // 122
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

/* a non-constant default (a function call). */
//-EXPECT-ERROR: A parameter default must be a constant expression.
//int neg_const(int a, b = one()) {
//    return a + b;
//}

