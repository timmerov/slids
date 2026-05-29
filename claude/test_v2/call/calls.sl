/*
test user-defined function calls.

scope per Phase 1 plan (todo.txt):
  * grammar: parameter lists (typed, comma-separated); 0 / 1 / N arg call syntax
  * classify: Function entries carry ordered param types; arity + per-arg type
    check at every call site; param-as-LocalVar entries seeded in the body frame
  * codegen: param allocas + stores at function entry; `call <ret> @name(args)`
  * calls are statement-form only; return values are not yet used in expressions
  * single-TU only — cross-TU calls (callee.slh + callee.sl) defer to a later landing

per-arg literal-flex: a literal arg flexes into its corresponding param type
the same way a literal var-decl init flexes into its declared type.
ident args widen via widen::convert.
*/

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

int32 main() {
    helper();              // zero-arg
    add(2, 3);             // two-arg
    mixed(7, 3.5, true);   // multi-arg mixed types
    caller();              // forward-call via pass-1 collection
    takes_int64(100);      // literal '100' flexes to int64 param
    int32 x = 7;
    takes_int64(x);        // int32 widens to int64 param

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

    return 0;
}
