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

int32 main() {

    Method method1(76);
    method1.print();
    //int x = method1.get();
    //__println("x = " + x);

    return 0;
}
