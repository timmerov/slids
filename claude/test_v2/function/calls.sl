/*
test user-defined function calls.

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

int32 main() {
    helper();              // zero-arg
    add(2, 3);             // two-arg
    mixed(7, 3.5, true);   // multi-arg mixed types
    caller();              // forward-call via pass-1 collection
    takes_int64(100);      // literal '100' flexes to int64 param
    int32 x = 7;
    takes_int64(x);        // int32 widens to int64 param

    greet();               // void statement-form call (implicit ret void)

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
