/*
test calling functions.
includes forward declarations (signature only).

the return type is required.
non-void functions must return on all code paths.
void functions may return on any code path.

primitive parameters (bool, char, intptr, intN, uintN, floatN, pointer) are
passed by value, all other types are passed by address.
passing parameters is one of the places where syntax does not exactly match semantics
- for both the function declaration and the call site.
the caller's data is not duplicated.
the function has direct access to the caller's data.

note: the following assumes const correctness has landed.

    alias Tuple = (int,int);
    alias Vec2 = int[2];
    Class(int field_) { }

    void tuple_fn1( (int,int)^ tpl );   // (const (int,int))^
    void tuple_fn2( Tuple^ tpl );       // (const Tuple)^
    void array_fn3( int arr[2] );       // (const int[2])^ - arr -> arr^
    void array_fn4( Vec2 arr );         // (const Vec2)^   - arr -> arr^
    void class_fn5( Class^ cls );       // const Class^

the call sites:

    (int,int) tpl1;
    Tuple tpl2;
    int arr1[2];
    Vec2 arr2;
    Class cls;

explicitly by pointer.

    tuple_fn1( ^tpl1 );
    tuple_fn1( ^tpl2 );
    tuple_fn2( ^tpl1 );
    tuple_fn2( ^tpl2 );
    class_fn5( ^cls );

auto-promoted to reference.
the tuple/array/class data is not copied.
it is not passed by value.

    tuple_fn1( tpl1 );
    tuple_fn1( tpl2 );
    tuple_fn2( tpl1 );
    tuple_fn2( tpl2 );
    array_fn3( arr1 );
    array_fn3( arr2 );
    array_fn4( arr1 );
    array_fn4( arr2 );
    class_fn5( cls );

arrays are a bit special because "array" means both the list of data - array-as-data:

    int arr[2];  <--- the data

and "array" also means the address of the data - array-as-pointer.

    void array_fn3(
        int arr[2]  <--- pointer to the data
    );
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
// a const-EXPRESSION dim in the RETURN type's TYPE position (`int[A3LEN] f()`, the
// size on the type not the name) — baked into the function's return type.
int[A3LEN] makeArrN() {
    int r[A3LEN] = (6, 7, 8);
    return r;
}
// a const-EXPRESSION dim in a tuple-slot PARAM type (type position, distinct from
// the name-anchored `int a[A3LEN]` form above).
int sumTupN((int[A3LEN], int)^ t) {
    return t^[0][0] + t^[0][1] + t^[0][2] + t^[1];
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

/* array by pointer. */
void array_by_pointer(int a[3]) {
    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
}

/* tuple by pointer. */
void tuple_by_pointer((int,int,int)^ t) {
    t^[0] = 40;
    t^[1] = 50;
    t^[2] = 60;
}

/* class by pointer. */
Class(int field_ = 0) { }
void class_by_pointer(Class^ cls) {
    cls^.field_ = 70;
}

/* alias-tuple / alias-array param. */
alias Pair = (int, int);
alias Vec2 = int[2];

void mutate_pair(Pair^ p) {
    p^[0] = 33;
    p^[1] = 44;
}

void mutate_vec(Vec2 v) {
    v[0] = 11;
    v[1] = 22;
}

/* multi-dim array param: `int g[2][3]` is rewritten to `int[2][3]^`, indexed
   without an explicit deref through the whole chain. */
void mutate_2d(int g[2][3]) {
    g[0][0] = 1;
    g[1][2] = 2;
}

/* sizeof on an array-by-pointer param: the BODY reads the POINTEE size, not the
   pointer size. `int a[3]` -> 12, not 8. */
intptr param_size(int a[3]) {
    return sizeof(a);
}

/* rvalue arm: a tuple LITERAL has no caller-side address, so the call site
   materializes it in a stacksaved temp and passes the temp's address. */
void echo_tpl((int,int,int)^ t) {
    __println("echo= " + t^[0] + " " + t^[1] + " " + t^[2]);
}

/* multi-arg call mixing lvalue + rvalue: `a` is a caller lvalue (mutates in
   place), `b` is an rvalue tuple literal (materialized in a temp; the function
   reads it but the caller can't see the write). */
void mix_call((int,int,int)^ a, (int,int,int)^ b) {
    __println("mix_b= " + b^[0] + " " + b^[1] + " " + b^[2]);
    a^[0] = 5;
}

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

    // const-expr dim in a TYPE-position return type and a tuple-slot param type
    int mN[A3LEN] = makeArrN();
    __println("makeArrN= " + mN[0] + " " + mN[2]);   // 6 8
    (int[A3LEN], int) tpN = ((1, 2, 3), 10);
    __println("sumTupN= " + sumTupN(tpN));            // 16

    /* expected: 10,20,30 */
    int a1[3] = (0,0,0);
    array_by_pointer(a1);
    __println("a1 = [" + a1[0] + "," + a1[1] + "," + a1[2] + "]");

    /* expected: 40,50,60 */
    (int,int,int) t1 = (0,0,0);
    tuple_by_pointer(t1);
    __println("t1 = (" + t1[0] + "," + t1[1] + "," + t1[2] + ")");

    /* expected: 40,50,60 */
    (int,int,int) t2 = (0,0,0);
    tuple_by_pointer(^t2);
    __println("t2 = (" + t2[0] + "," + t2[1] + "," + t2[2] + ")");

    /* expected: 70 */
    Class cls1;
    class_by_pointer(cls1);
    __println("cls1.field_ = " + cls1.field_);

    /* expected: 70 */
    Class cls2;
    class_by_pointer(^cls2);
    __println("cls2.field_ = " + cls2.field_);

    /* alias-tuple convenience — lvalue arm through an alias type. expected: 33 44 */
    Pair pp = (0, 0);
    mutate_pair(pp);
    __println("pp = " + pp[0] + " " + pp[1]);

    /* alias-array convenience — lvalue arm through an array alias. expected: 11 22 */
    Vec2 va = (0, 0);
    mutate_vec(va);
    __println("va = " + va[0] + " " + va[1]);

    /* multi-dim array param. expected: 1 2 */
    int gg[2][3] = ((0,0,0), (0,0,0));
    mutate_2d(gg);
    __println("gg = " + gg[0][0] + " " + gg[1][2]);

    /* sizeof on an array-by-pointer param: pointee size, not pointer size.
       expected: 12 */
    int p3[3] = (0,0,0);
    __println("param_size = " + param_size(p3));

    /* rvalue arm — a tuple LITERAL materializes a temp at the call site.
       expected: echo= 9 8 7 */
    echo_tpl((9, 8, 7));

    /* indexed lvalue — an array element passed by convenience. The array's
       element address goes directly to the function; the function mutates it.
       expected: t_arr[0] = (40,50,60) */
    (int,int,int) t_arr[2] = ((0,0,0), (0,0,0));
    tuple_by_pointer(t_arr[0]);
    __println("t_arr[0] = (" + t_arr[0][0] + "," + t_arr[0][1] + "," + t_arr[0][2] + ")");

    /* deref-lvalue — `pt^` is an lvalue (reaches the pointee). Passing it by
       convenience takes the pointee's address, not a copy. expected: t3 = (40,50,60) */
    (int,int,int) t3 = (0,0,0);
    (int,int,int)^ pt = ^t3;
    tuple_by_pointer(pt^);
    __println("t3 = (" + t3[0] + "," + t3[1] + "," + t3[2] + ")");

    /* multi-arg mixed lvalue + rvalue: `mx` is mutated in place; `(7,8,9)` is
       a temp the function reads (and writes, invisibly to the caller).
       expected: mix_b= 7 8 9 then mx[0] = 5 */
    (int,int,int) mx = (0, 0, 0);
    mix_call(mx, (7, 8, 9));
    __println("mx[0] = " + mx[0]);

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

/* a class value has no conversion to a mismatched PARAMETER type — the same reject
   as a var-decl/assignment, now applied at the call site. */
//-EXPECT-ERROR: Cannot implicitly convert 'C' to 'int'
//C(int x_) { _(){} ~(){} }
//int takesInt(int n) { return n; }
//int neg_class_arg() {
//    C v(1);
//    return takesInt(v);
//}

/* ...and at a RETURN whose value's type doesn't match the function's return type. */
//-EXPECT-ERROR: Cannot implicitly convert 'C' to 'int'
//C(int x_) { _(){} ~(){} }
//int neg_class_return() {
//    C v(1);
//    return v;
//}

/* a TUPLE value parameter is rejected — non-primitive params must be passed by
   pointer (or via the array shorthand). */
//-EXPECT-ERROR: A non-primitive parameter must be a pointer (reference / iterator) or an array
//void neg_tuple_value_param((int,int) t) { }

/* same for a CLASS value parameter. */
//-EXPECT-ERROR: A non-primitive parameter must be a pointer (reference / iterator) or an array
//Cv(int x_) { _(){} ~(){} }
//void neg_class_value_param(Cv c) { }

/* class types are NOMINAL — Class1 and Class2 are distinct even with identical
   field layouts, so `Class2^` does not pass to a `Class1^` param. */
//-EXPECT-ERROR: Cannot implicitly cast 'Class2^' to '(const Class1)^'
//Class1(int x_) { }
//Class2(int x_) { }
//void neg_class_mismatch_fn(Class1^ p) { }
//int neg_class_mismatch() {
//    Class2 cm(1);
//    neg_class_mismatch_fn(^cm);
//    return 0;
//}
