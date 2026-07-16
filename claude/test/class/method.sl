/*
test class methods.

functions declared in a class body are methods.
the object fields are accessible within method scope.

methods have the same features and restrictions as bare functions.
*/

/*
claude says:

a method is a self-bound member FUNCTION. The grammar parses it like any function
and injects an implicit receiver param `_$recv` (Class^) as params[0]; `self` is a
separate address-aliased local whose STORAGE is `_$recv`'s target, so bare field
references resolve to `self` / `_$recv^.field` (buildSelfField), and the method
shares the ctor/dtor resolve/classify path (the forEachHoistedClass walker, filtered
on kFunctionDef). The method lifts to a top-level `<Class>__method(_$recv, ...)`. A
call `obj.method(args)` parses to kMethodCallStmt (receiver = children[0]); desugar
lowers it to a normal call of the lifted symbol with `^receiver` prepended as
`_$recv`. Methods work in file-scope, hoisted, and local classes alike.

bare field READS, plain `=` WRITES, AND compound writes (`x_ += 1`, `x_++`) all
rewrite to a `self` / `_$recv^.field` lvalue, uniformly in ctor/dtor/method (fields
are initialized by construction before any body runs, so a field is just an lvalue
everywhere). Reads and writes share buildSelfField.

both call forms LANDED: the STATEMENT form (`obj.method(args);`) and the EXPRESSION
form (a value-returning method used as a value, `x = obj.m()`). The receiver may be
a named local, a deref'd pointer (`p^.m()`), an array element (`a[i].m()`), a
class-typed field (`o.in_.m()`), a construction temp (`Class(2).m()`), or another
method's BY-VALUE class RESULT (`a.step().get()`) — a class-valued call result is
materialized into a `_$cret` temp (desugar's liftSretCallExprs, gated on
returnTypeHasClassValue) so the next call has an ADDRESSABLE receiver and the temp
is destroyed at the semicolon; a TRIVIAL class (no hooks) is lifted too, since the
receiver address is needed even when there is nothing to destroy.

SIBLING VISIBILITY: a method / ctor / dtor body calls sibling methods BARE — no
`self.`, no qualifier — exactly as it reads fields bare. Author `m()` IS author
`self.m()` (= compiler `_$recv^.m()`); a bare field `f_` IS author `self.f_` (=
compiler `_$recv^.f_`). Both route through the receiver, so a bare sibling call
has the SAME reach as a bare field access — it works wherever a bare field works,
including inside a NESTED function in the method. `self.m()` stays valid and is
REQUIRED only when a local legally SHADOWS the method (then a bare `m()` binds the
local, not the method — exactly as a bare field is shadowed by a same-named local).
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

/* a method on a HOISTED class — including a BARE sibling call (`dbl()`) inside the
   nested class, reaching the same way it reaches the bare field `n_`. */
Host(int h_) {
    Inner(int n_) {
        int  dbl()  { return n_ + n_; }
        void show() { __println("Inner:show: " + n_ + " dbl=" + dbl()); }
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

/* a method that returns its OWN class BY VALUE, so calls CHAIN (`obj.next().get()`).
   The intermediate result is materialized into a temp whose ADDRESS the next call
   needs; with hooks it dies at the semicolon. */
Chain(int v_) {
    _() { __println("Chain:ctor: " + v_); }
    ~() { __println("Chain:dtor: " + v_); }
    Chain next() { Chain r(v_ + 1); return r; }
    int   get()  { return v_; }
}

/* a TRIVIAL class (no ctor/dtor) whose method returns the class BY VALUE — chaining
   must still materialize the receiver even with nothing to destroy. `run` chains
   off a BARE sibling (`step().step().get()`) — the rewrite feeds the sret + chain
   machinery exactly like an external receiver does. */
Trin(int v_) {
    Trin step() { Trin r(v_ + 1); return r; }
    int  get()  { return v_; }
    int  run()  { return step().step().get(); }
}

/* SIBLING VISIBILITY — every member body reaches sibling methods BARE. The ctor
   and dtor call siblings; a method calls siblings as a statement, as a value, and
   with arguments; a sibling calls a further sibling; `self.m()` still works as the
   explicit spelling. All bare calls sit next to bare FIELD reads of the same x_. */
Sibs(int x_) {
    _()  { __println("Sibs:ctor"); seed(); }            // ctor -> sibling
    ~()  { bump(); __println("Sibs:dtor: " + x_); }     // dtor -> sibling
    void seed()     { x_ = 100; }
    void bump()     { x_ = x_ + 1; }
    void add(int d) { x_ = x_ + d; }                    // sibling taking an arg
    int  peek()     { return x_; }                      // value-returning sibling
    void report()   { __println("Sibs:report: " + peek()); }   // sibling -> sibling
    void run() {
        __println("Sibs:run x_=" + x_);   // bare FIELD read beside the bare calls
        add(5);                           // bare sibling STATEMENT, with an arg
        int v = peek();                   // bare sibling as an EXPRESSION
        __println("Sibs:peek=" + v);
        report();                         // bare sibling that itself calls a sibling
        self.add(0);                      // explicit self.method() is still valid
    }
}

/* a bare sibling call (and a bare field read) from inside a NESTED function in a
   method — same reach as the enclosing body, since both go through the receiver. */
Nest(int x_) {
    int dbl()  { return x_ + x_; }
    int run()  { int via() { return dbl() + x_; }   // nested fn: bare sibling + bare field
                 return via(); }
}

/* `self.method()` is REQUIRED when a local SHADOWS the method name: the bare form
   binds the local (here the int m), so the explicit self form reaches the method. */
Shadow(int x_) {
    int m()   { return 7; }
    int run() { int m = 3; return m + self.m(); }   // 3 + 7 -> 10
}

/* bare sibling calls inside CONTROL FLOW (if / while conditions and bodies), plus
   a value-returning sibling whose result is DISCARDED as a bare statement. */
Flow(int x_) {
    void step() { x_ = x_ + 1; }
    int  get()  { return x_; }
    int  tick() { x_ = x_ + 1; return x_; }
    void run() {
        if (get() < 3) { step(); }        // bare sibling in an if cond + body
        while (get() < 5) { step(); }      // bare sibling in a while cond + body
        tick();                            // value-returning sibling, result discarded
        __println("Flow:run: " + get());   // 6
    }
}

/* bare SELF-recursion and bare MUTUAL recursion between siblings. */
Rec(int x_) {
    int sum(int n)    { if (n <= 0) { return 0; } return n + sum(n - 1); }   // self
    int isEven(int n) { if (n == 0) { return 1; } return isOdd(n - 1); }     // mutual
    int isOdd(int n)  { if (n == 0) { return 0; } return isEven(n - 1); }
    void run() { __println("Rec:run: sum4=" + sum(4) + " even4=" + isEven(4)); }  // 10, 1
}

/* a bare sibling FORWARD-referenced — `foo` calls `bar`, declared later. */
Fwd(int x_) {
    int  foo() { return bar() + 1; }   // bar declared AFTER foo
    int  bar() { return x_; }
    void run() { __println("Fwd:run: " + foo()); }   // x_=41 -> 42
}

/* a bare sibling call passing a bare FIELD as an arg, MULTIPLE args, and a non-int
   (bool) arg. */
Args(int x_) {
    int  add2(int a, int b)          { return a + b; }
    int  pick(bool hi, int a, int b) { if (hi) { return a; } return b; }
    void run() { __println("Args:run: " + add2(x_, 5) + " " + pick(true, x_, 0)); }  // 14, 9
}

/* a method FORWARD DECLARATION followed by its definition — parity with a bare
   function. The decl and def are SEPARATE entries matched by signature, so the decl
   is not an orphan and the decl+def pair is not an ambiguous overload. A distinct-
   signature overload coexists with the forward-declared one. */
FwdDecl(int x_) {
    int twice(int n);                       // forward declaration
    int twice(int n)        { return n * 2; }   // its definition
    int twice(int n, int k) { return n * k; }   // a distinct overload
    void run() { __println("FwdDecl:run: " + twice(x_) + " " + twice(x_, 3)); }  // 10 15
}

Forward(int x_) {
    void print();
    void print() {
        __println("Forward:print: " + x_);
    }
}

int32 main() {

    Method method1(76);
    method1.print();
    // a value-returning method used as an EXPRESSION (not just a discarded statement).
    int x = method1.get();
    __println("x = " + x);

    // a method with parameters, called as a statement.
    Pt p(1, 2);
    p.shift(10, 20);

    // a method on a hoisted class.
    Host:Inner inner(7);
    inner.show();

    // a method on a LOCAL class — with a BARE sibling call (`trip()`) inside it.
    {
        Loc(int v_) {
            int  trip() { return v_ + v_ + v_; }
            void emit() { __println("Loc:emit: " + v_ + " trip=" + trip()); }
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

    // CHAINED method call in a DECL initializer: each link returns a class BY VALUE,
    // so the next call's receiver is a temporary materialized into a `_$cret` slot.
    // ctors run inner-to-outer (1 then 2). The chain TEMPS are statement-scoped (the
    // scalar-valued rhs is wrapped in a seq): they die at the end of the DECL
    // statement, 2 then 1 in reverse order, while the named local c(0) dies at
    // scope end.
    {
        Chain c(0);
        int chain = c.next().next().get();   // 2
        __println("chain = " + chain);
        __println("-- end chain (dtor 2,1 ran at stmt; dtor 0 next, at scope end) --");
    }

    // CHAINED method call as a STATEMENT (the result discarded): the intermediate
    // receiver temp is STATEMENT-scoped, so it dies at the SEMICOLON (before the
    // named `d` dies at scope end).
    {
        Chain d(5);
        d.next().get();   // 6 discarded; the next() temp dies at the ';'
        __println("-- mid chain-stmt (dtor 6 ran; dtor 5 at scope end) --");
    }

    // chaining on a TRIVIAL class (no hooks): the receiver is still materialized, so
    // a deeper chain just yields the final value.
    Trin t(10);
    __println("trin = " + t.step().step().get());   // 12
    __println("trin-bare = " + t.run());            // bare step().step().get() -> 12

    // SIBLING VISIBILITY: bare sibling-method calls (no self, no qualifier). The
    // ctor's seed() sets x_=100; run() does add(5)->105, peek()->105, report()->105;
    // the dtor's bump() makes x_=106 at scope end.
    Sibs sib(0);
    sib.run();

    // a bare sibling call inside a nested function: dbl()=10 plus the field x_=5.
    Nest nst(5);
    __println("Nest:run = " + nst.run());            // 15

    // self.method() reaching a method past a shadowing local.
    Shadow shd(0);
    __println("Shadow:run = " + shd.run());          // 10

    // bare siblings inside control flow + a discarded value-call.
    Flow flw(0);
    flw.run();                                       // Flow:run: 6

    // bare self-recursion and mutual recursion.
    Rec rec(0);
    rec.run();                                       // Rec:run: sum4=10 even4=1

    // a forward-referenced bare sibling.
    Fwd fwd(41);
    fwd.run();                                        // Fwd:run: 42

    // bare field arg, multiple args, and a bool arg to bare siblings.
    Args arg(9);
    arg.run();                                        // Args:run: 14 9

    FwdDecl fd(5);
    fd.run();                                         // FwdDecl:run: 10 15

    Forward forward;
    forward.print();

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

/* a bare sibling call whose name is SHADOWED by a local binds the local, not the
   method — and a local is not callable. (Reach the method with `self.m()`.) This
   mirrors a bare field shadowed by a same-named local. */
//-EXPECT-ERROR: 'm' is a variable, not a function
//ShadowBad(int x_) {
//    int m()   { return 7; }
//    int run() { int m = 3; return m(); }
//}

/* a PARAM shadows a sibling method name (a param is a body local) — same as the
   local-shadow case: the bare call binds the param, which is not callable. */
//-EXPECT-ERROR: 'm' is a variable, not a function
//ParamShadowBad(int x_) {
//    int m()        { return 7; }
//    int run(int m) { return m(); }
//}

/* a method DECLARED but never DEFINED is an orphan — parity with a bare function.
   (A same-signature definition would satisfy it; here there is none.) */
//-EXPECT-ERROR: declared but never defined
//Orphan(int x_) {
//    int ghost(int n);
//}

/* a paren-less method reference is not a value. As a STATEMENT `m.get;` is rejected
   (an lvalue with no '='). */
//-EXPECT-ERROR: Expected '='
//int32 neg_method_stmt() {
//    Method m(0);
//    m.get;
//    return 0;
//}

/* ...and in VALUE position `int v = m.get;` the call is missing its parameter list —
   it is NOT an implicit call. */
//-EXPECT-ERROR: Function call is missing parameter list
//int32 neg_method_value() {
//    Method m(0);
//    int v = m.get;
//    return v;
//}
