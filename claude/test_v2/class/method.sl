/*
test class methods.

functions declared in a class body are methods.
the object fields are accessible within method scope.

methods have the same features and restrictions as bare functions.
*/

/*
claude says:

a method is a self-bound member FUNCTION — a ctor/dtor with a user name. The
grammar parses it like any function and injects an implicit `self` (Class^) as
params[0]; the body is a full function body, so bare field names rewrite to
`self^.field` (method_fields), and it shares the ctor/dtor resolve/classify path
(the forEachHoistedClass walker, filtered on kFunctionDef). The method lifts to a
top-level `<Class>__method(self, ...)`. A call `obj.method(args)` parses to
kMethodCallStmt (receiver = children[0]); desugar lowers it to a normal call of the
lifted symbol with `^receiver` prepended as self. Methods work in file-scope,
hoisted, and local classes alike.

a bare field WRITE (`x_ = ...`) rewrites to a `self^.field` store, the mirror of
the read rewrite — uniformly in ctor/dtor/method (fields are initialized by
construction before any body runs, so a field is just an lvalue everywhere). Both
sides share buildSelfField.

scope of this landing: the STATEMENT call form (`obj.method(args);`) and plain `=`
field writes. Known follow-ups: the EXPRESSION call form (a value-returning method
used as a value, `x = obj.m()`); and compound field writes — `x_ += 1`, `x_++` —
whose statement paths aren't field-rewritten yet.
*/

Method(int x_) {
    _() {
        x_ = x_ + 1;
        __println("Method:ctor: " + x_);
    }
    ~() {
        x_ = x_ + 1;
        __println("Method:dtor: " + x_);
    }

    void print() {
        x_ = x_ + 1;
        __println("Method:print: " + x_);
    }

    int get() {
        x_ = x_ + 1;
        return x_;
    }
}

/* a method with PARAMETERS, on a multi-field class; the args ride alongside the
   implicit self. */
Pt(int x_, int y_) {
    void shift(int dx, int dy) {
        x_ = x_ + dx;
        y_ = y_ + dy;
        __println("Pt:shift: " + x_ + "," + y_);
    }
}

/* a method on a HOISTED class. */
Host(int h_) {
    Inner(int n_) {
        void show() { __println("Inner:show: " + n_); }
    }
}

int dbl(int a) { return a + a; }   // a free function, called from a method

/* a method body has the full function feature set: it can call a FREE function,
   take a CLASS-typed param, and define a NESTED function. */
B(int v_) { }
Use(int u_) {
    void scaleUp()    { u_ = dbl(u_); __println("Use:scaleUp: " + u_); }
    int  peek()       { return u_; }                       // value method
    void absorb(B^ b) { u_ = u_ + b^.v_; __println("Use:absorb: " + u_); }
    void viaNested()  { int sq(int a) { return a * a; }    // nested fn in a method
                        u_ = sq(u_); __println("Use:viaNested: " + u_); }
}

int32 main() {

    Method method1(76);
    method1.print();
    //int x = method1.get();
    //__println("x = " + x);

    // a method with parameters, called as a statement.
    Pt p(1, 2);
    p.shift(10, 20);

    // a method on a hoisted class.
    Host:Inner inner(7);
    inner.show();

    // a method on a LOCAL class.
    {
        Loc(int v_) {
            void emit() { __println("Loc:emit: " + v_); }
        }
        Loc l(3);
        l.emit();
    }

    // a method calling a free function.
    Use use(5);
    use.scaleUp();           // u_ = 10
    // a value-returning method called as a statement (return discarded).
    use.peek();
    // a method with a class-typed param (by reference).
    B b(3);
    use.absorb(^b);          // u_ = 13
    // a nested function inside a method body.
    use.viaNested();         // u_ = 169

    // method calls through a deref'd pointer and an array-element receiver.
    Use^ up = ^use;
    up^.scaleUp();           // u_ = 338
    Use arr[2](1, 2);
    arr[1].scaleUp();        // arr[1].u_ = 4

    return 0;
}

/* compiler errors. */

/* a method that the class does not declare. */
//-EXPECT-ERROR: has no method 'nope'
//int32 neg_no_method() {
//    Method m;
//    m.nope();
//    return 0;
//}

/* the wrong number of arguments to a method. */
//-EXPECT-ERROR: expects 2 arguments, got 1
//int32 neg_arity() {
//    Pt q(0, 0);
//    q.shift(1);
//    return 0;
//}

/* a method call on a non-class value. */
//-EXPECT-ERROR: requires a class object
//int32 neg_non_class() {
//    int x = 5;
//    x.foo();
//    return 0;
//}

/* a bare sibling-method call has no receiver — calling a method through `self` is
   deferred, so reject it cleanly (must not lower a self-less call). */
//-EXPECT-ERROR: Method 'inc' must be called on an object
//BadCall(int x_) {
//    void inc() { x_ = x_ + 1; }
//    void twice() { inc(); }
//}
