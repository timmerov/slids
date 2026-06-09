/*
test calling functions.
includes forward declarations (signature only).

the return type is required.
non-void functions must return on all code paths.
void functions may return on any code path.
*/

/*
claude says:

scope per Phase 1 plan (todo.txt):
  * grammar: parameter lists (typed, comma-separated); 0 / 1 / N arg call syntax
  * classify: Function entries carry ordered param types; arity + per-arg type
    check at every call site; param-as-LocalVar entries seeded in the body frame
  * codegen: param allocas + stores at function entry; `call <ret> @name(args)`
  * calls work as statements (result discarded) and as expressions (kCallExpr:
    var-decl init, call arg, return, binary operand); the expression form
    widens its return value into the destination type
  * single-TU only — cross-TU calls (callee.slh + callee.sl) defer to a later landing

per-arg literal-flex: a literal arg flexes into its corresponding param type
the same way a literal var-decl init flexes into its declared type.
ident args widen via widen::convert.
*/

// forward declaration (signature only); the definition is at the bottom of the
// file, and it is both called and forward-referenced before that definition.
int32 fwd_decl(int32 n);

int32 helper() {
    __println("helper");
    return 0;
}

int32 add(int32 a, int32 b) {
    int32 r = a + b;
    __println("add= " + r);
    return 0;
}

int32 mixed(int32 i, float32 f, bool b) {
    __println("i= " + i);
    __println("f= " + f);
    __println("b= " + b);
    return 0;
}

// uses a function defined later in the file — exercises pass-1 forward collection.
int32 caller() {
    callee();
    return 0;
}

int32 callee() {
    __println("callee");
    return 0;
}

int32 takes_int64(int64 v) {
    __println("v64= " + v);
    return 0;
}

int32 takes_int32(int32 v) {
    __println("v32= " + v);
    return 0;
}

// value-returning helpers (no side-effect print) — exercise calls used as
// expressions: their return value is the thing under test.
int32 sum(int32 a, int32 b) {
    return a + b;
}

int32 doubled(int32 x) {
    return sum(x, x);          // call in return position
}

// array params + an array return: an array arg passes the aggregate. An array
// PARAM carries its size on the NAME (`int a[3]`, the same form as a var decl) or
// via an alias (`A3 a`), with a literal or a const-EXPRESSION dim; `int[3] f()` is
// the only way to RETURN an array.
const int A3LEN = 3;
alias A3 = int[3];
int[3] makeA3() {
    int r[3] = (3, 4, 5);
    return r;
}
int sumA3(A3 a) {              // alias param
    return a[0] + a[1] + a[2];
}
int arrSum(int a[3]) {        // name-anchored, literal dim
    return a[0] + a[1] + a[2];
}
int arrSumN(int a[A3LEN]) {   // name-anchored, const-expression dim
    return a[0] + a[1] + a[2];
}

/*
orphan function declared but not defined.
valid in a header.
presumably it will be defined in a different file.
or it's a link error.
compile error in a source file.
regardless of whether it's used anywhere or not.
*/
//-EXPECT-ERROR: declared but never defined
//int declared_not_defined();

/* a definition whose return type disagrees with an earlier declaration (same
   parameters — not overloadable on return type alone). */
//-EXPECT-ERROR: Return type 'int64' does not match earlier declaration's 'int32'.
//int32 mismatch_ret(int32 a);
//int64 mismatch_ret(int32 a) { return 0; }

int32 main() {
    helper();              // zero-arg
    add(2, 3);             // two-arg
    mixed(7, 3.5, true);   // multi-arg mixed types
    caller();              // forward-call via pass-1 collection
    takes_int64(100);      // literal '100' flexes to int64 param
    int32 x = 7;
    takes_int64(x);        // int32 widens to int64 param

    greet();               // void statement-form call (implicit ret void)
    say_if_pos(1);         // void with an early bare `return;` (takes the return)
    say_if_pos(-1);        // ... and the fall-through path

    // call as expression (kCallExpr)
    int32 s = sum(2, 3);             // call as var-decl init
    __println("s= " + s);
    int32 n = sum(sum(1, 2), 4);     // nested call as arg
    __println("n= " + n);
    int64 w = sum(10, 20);           // int32 return widens to int64 init
    __println("w= " + w);
    int32 e = sum(40, 1) + sum(1, 1);  // calls as binary operands
    __println("e= " + e);
    __println("d= " + doubled(21));  // call (return-position result) as print arg
    __println("fwd= " + fwd_decl(9));  // forward-declared above, defined below

    // array args + an array return
    int m[3] = makeA3();
    __println("makeA3= " + m[0] + " " + m[2]);   // 3 5
    __println("sumA3= " + sumA3(m));             // 12
    __println("arrSum= " + arrSum(m));           // 12
    __println("arrSumN= " + arrSumN(m));         // 12

    //-EXPECT-ERROR: expects 2 arguments, got 1
    //add(1);

    //-EXPECT-ERROR: expects 2 arguments, got 3
    //add(1, 2, 3);

    //-EXPECT-ERROR: Cannot implicitly narrow
    //int64 big = 5;
    //takes_int32(big);

    //-EXPECT-ERROR: Unknown function
    //no_such_fn();

    //-EXPECT-ERROR: Cannot assign to function
    //helper = 5;

    //-EXPECT-ERROR: is a variable, not a function
    //int32 v = 0;
    //v();

    //-EXPECT-ERROR: returns no value and cannot be used as an expression
    //int32 nv = greet();
    //__println("nv= " + nv);

    //-EXPECT-ERROR: '__println' cannot be used as an expression
    //int32 pv = __println("x");

    return 0;
}

// void function — its body falls through, exercising the implicit `ret void`
// terminator; called as a statement below, and the target of the
// void-as-value negative case.
void greet() {
    __println("greet");
}

// a void function with a BARE `return;` — an early exit on one path, falling
// through (implicit ret void) on the other. "void may return on any path."
void say_if_pos(int32 n) {
    if (n > 0) {
        __println("pos");
        return;
    }
    __println("nonpos");
}

// the definition matching the forward declaration at the top of the file.
int32 fwd_decl(int32 n) {
    return n + n;
}

// file-scope negatives — whole function definitions, so they can't sit inside
// main (no nested functions yet).

//-EXPECT-ERROR: must end with a return statement
//int32 falls_through() {
//    __println("no return here");
//}

//-EXPECT-ERROR: A void function cannot return a value
//void returns_value() {
//    return 0;
//}

/* a bare `return;` is only valid in a void function. */
//-EXPECT-ERROR: A non-void function must return a value.
//int32 bare_return_nonvoid() {
//    return;
//}

/* a local may not shadow a parameter — the parameter shares the body's top
   scope, so a same-named body-top local is a duplicate declaration. */
//-EXPECT-ERROR: Duplicate declaration of 'x'
//void shadow_ban(int x) {
//    int x = 0;
//    return x;
//}

/* two definitions of the same function is a duplicate definition (a forward
   declaration first is fine; a second body is not). */
//-EXPECT-ERROR: Duplicate definition of 'twice'.
//int twice(int a);
//int twice(int a) {
//    return a;
//}
//int twice(int a) {
//    return a + 1;
//}

/* an array size in TYPE position is rejected for a parameter, same as a var decl —
   the size belongs on the NAME (`int p[3]`). */
//-EXPECT-ERROR: An array size belongs on the declared name
//int neg_array_param(int[3] p) {
//    return p[0];
//}

/* a name-anchored param dim must be a CONSTANT expression — a function call is not
   foldable, so the size is rejected. */
//-EXPECT-ERROR: Array size must be an integer constant
//int neg_param_dim_nonconst(int a[helper()]) {
//    return a[0];
//}
