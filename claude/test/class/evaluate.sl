/*
optimize the evaluation of expressions that need temporary class objects.

    String s = String + a + b + c + d;

if the String class defines op= for a's type and op+= for b,c,d's types
then that entire variable declaration statement can be evalutated with
zero temporary String objects.

    s = a; s += b; s += c; s += d;

in cases where lhs is existing object variable and rhs is a temp,
move (not copy) semantics should be used when possible.

    String s; s = String + a + b + c + d;
    temp = a; temp += b; temp += c; temp += d; s <-- temp;

notes:

some things deferred from other landings that might apply here:

  - Copy-into-target instead of move — the expression-temp is copied into an existing-var target (one extra copy).
  - only-op+= seed — a class with op+= but no 2-arg op+ can't seed a fused chain.
  - Ping-pong two-buffer chains (only op+, no op+=).

check the dtor for these temps run at the end of the statement.
not the end of the enclosing scope.
i think we should be using the sequence-expression machinery here.
seq-exp = ( init/construct, function-call, destruct )

    int x = fn(String + "Hello");
    x = fn(String + "Hello");
    fn(String + "Hello");

this produces 11 ctor/dtors.

    ((a, b), c) = ((Class(1), Class(2)), Class(3));

does not compile:

    ((a, b), c) <--  ((Class(1), Class(2)), Class(3));
    ((a, b), c) <--> ((Class(1), Class(2)), Class(3));

some claude detritus:

  - Chains a = b + c + d — the whole fuse/fresh/seed/ping-pong design, and the elide-if-fresh / move-if-existing / never-copy
  tail. Chains currently error (cleanly — not a miscompile).
  - Unary (a = -b → a.op-(b) is an easy 1-arg rewrite; if(-a) → a.op-() is stage-4-like).
  - Aliasing a = a + c (the direct rewrite reads+writes a — needs a temp).
  - Decl-init binary Class r = a + b (same kVarDeclStmt gap as decl-init op=).
  - Only-op+= head (seed via op=) and ping-pong (only op+).
  - User op<-- for the =-class-rvalue implicit move (class rvalues currently take the existing default move/elide — wiring a
  user op<-- into that return/move path is separate). [i think this is use move semantics

*/

/*
claude says:

these tests cover the evaluation cases that are currently CORRECT: a temp
created to evaluate an expression has its dtor run at the end of the phrase
(the statement, or a condition's evaluation), not at the end of the enclosing
scope. a NESTED arg temp in a var-decl / assign / return rhs whose VALUE is a
scalar is wrapped in a kSeqExpr, so its dtor also runs at the statement -- this
was the bug of bug.sl.

cases B/C/E/F/G/H/I: the discarded-call, method-receiver, condition, and basic
scalar-decl paths. cases 1-8: the seq-wrap fix across statement kinds and edges
-- 1 assign rhs, 2 return rhs (temp dies before the fn unwinds its locals), 3a/3b
a ++ in the value child / in the temp's construction arg (the lowerInPhrase
assert-relaxation path), 4 two reverse-ordered temps, 5 a loop-body decl (per-
iteration, no growth), 6 a pointer-valued rhs (the guard's kPointer arm), 7 the
guard-negative boundary (a CLASS-valued rhs leaks its temp -- pinned as the
current deferred behavior), 8 aug-assign (lowers to assign, so covered too).

deliberately NOT correct yet (still-broken / deferred; case 7 pins the first):
  - a var-decl / return rhs whose VALUE is a CLASS built in place
    (Class y = make(Class(80));) still leaks its arg temp -- the seq wrap is
    scalar-only (a class value takes the in-place sret path).
  - a bare construction discard (Class(5);) escapes to enclosing scope.
  - the op+ chain (Class cd = ca + cb + cc) -- errors cleanly, not landed.
  - tuple-construct assignment (((a,b),c) = (...)) -- value-correct but
    spews redundant temps; not clean.
*/

Class(int a_) {
    _() { __println("Class:ctor: " + a_); }
    ~() { __println("Class:dtor: " + a_); }
    void inc() { ++a_; }
}

int fn(Class^ c) {
    c^.inc();
    return c^.a_;
}

int fn2(Class^ x, Class^ y) {
    return x^.a_ + y^.a_;
}

// a scalar-returning function with a NAMED local, used to observe that a temp in
// a `return fn(Class(n))` rhs is destroyed BEFORE the function unwinds its locals.
int ret_rhs() {
    Class keep = Class(70);
    return fn(Class(71));
}

// a POINTER-returning function: exercises the guard's kPointer arm (a class temp
// arg to a call whose VALUE is a pointer is still seq-wrapped).
int^ ptr_rhs(Class^ c, int^ p) {
    c^.inc();
    return p;
}

// a CLASS-returning function (built in place / RVO): the guard-negative boundary --
// its class temp arg is NOT seq-wrapped, so the temp keeps enclosing-scope lifetime.
Class make(Class^ c) {
    c^.inc();
    return Class(c^.a_ + 100);
}

int32 main() {

    // B: a temp as the sole arg of a discarded call statement.
    //    the temp dies at the semicolon (before "B end").
    __println("B: bare call, one temp arg");
    fn(Class(10));
    __println("B end");

    // C: two temp args of a discarded call statement.
    //    both die at the semicolon, in reverse construction order.
    __println("C: bare call, two temp args");
    fn2(Class(1), Class(2));
    __println("C end");

    // E: a discarded method call on a temp receiver.
    //    the receiver temp dies at the semicolon.
    __println("E: method call on temp receiver");
    Class(3).inc();
    __println("E end");

    // F: a nested arg temp in a var-decl rhs whose VALUE is a scalar. The temp
    //    dies at the end of the DECLARATION (before "F end"), while the declared
    //    variable x keeps its own (scope) lifetime. This was the bug of bug.sl.
    __println("F: nested temp in a scalar var-decl rhs");
    int x = fn(Class(4));
    __println("F end x=" + x);

    // G: a construction as the direct rhs of a declaration builds in place --
    //    NO temp is created; cd lives to the end of the scope.
    __println("G: build into declared var (no temp)");
    Class cd = Class(9);
    __println("G end cd=" + cd.a_);

    // H: a temp in an if-condition dies at the condition-phrase exit,
    //    BEFORE the body runs.
    __println("H: temp in if-condition");
    if (fn(Class(20)) > 0) {
        __println("H body");
    }
    __println("H end");

    // I: a temp in a while-condition is rebuilt and destroyed on EACH
    //    evaluation of the condition (here: twice -- one true, one false).
    __println("I: temp in while-condition");
    int n = 0;
    while (fn(Class(30)) > 0 && n < 1) {
        __println("I body");
        n = n + 1;
    }
    __println("I end");

    // ---- coverage for the seq-wrap fix: a nested arg temp in a scalar-valued
    //      var-decl / assign / return rhs dies at the STATEMENT (value + timing). ----

    // 1: ASSIGN rhs (x already declared). The temp dies at the assignment
    //    statement (dtor before "1 end"); x holds the value.
    __println("1: assign rhs");
    {
        int x1 = 0;
        x1 = fn(Class(10));
        __println("1 end x1=" + x1);          // expect dtor 11 BEFORE this
    }

    // 2: RETURN rhs. The temp is destroyed BEFORE the function unwinds its named
    //    local -- so inside ret_rhs the order is: ctor 70(keep), ctor 71(temp),
    //    dtor 72(temp, at the return), dtor 70(keep, on unwind).
    __println("2: return rhs (ordering inside ret_rhs)");
    {
        int r = ret_rhs();
        __println("2 end r=" + r);
    }

    // 3a: a ++ in the VALUE child of the seq-wrapped rhs. The temp dies at the
    //     statement; the post-inc of a applies (a: 100 -> 101).
    __println("3a: PPID in the value child");
    {
        int a = 100;
        int x = fn(Class(30)) + a++;
        __println("3a end x=" + x + " a=" + a);   // 31 + 100 = 131, a=101
    }

    // 3b: a ++ INSIDE the temp's construction arg (the assert-relaxation path:
    //     lowerInPhrase recurses into the seq's temp-decl init). Class takes b's
    //     value (40), then b bumps to 41; the temp dies at the statement.
    __println("3b: PPID in the temp construction arg");
    {
        int b = 40;
        int x = fn(Class(b++));
        __println("3b end x=" + x + " b=" + b);   // temp 40->inc 41, b=41
    }

    // 4: two NON-chained temps in one decl rhs -- both die at the statement, in
    //    reverse construction order (2 then 1).
    __println("4: two temps in a decl rhs");
    {
        int x = fn2(Class(1), Class(2));
        __println("4 end x=" + x);                // reverse dtor 2,1 BEFORE this
    }

    // 5: a seq-wrapped decl in a LOOP body -- each iteration constructs and
    //    destroys its own temp at the statement (no leak / no stack growth).
    __println("5: seq-wrapped decl in a loop body");
    {
        int i = 0;
        while (i < 2) {
            int xl = fn(Class(50 + i));
            __println("5 body xl=" + xl);          // dtor (51/52) BEFORE this
            i = i + 1;
        }
        __println("5 end");
    }

    // 6: a POINTER-valued rhs (guard's kPointer arm) -- the class temp is still
    //    seq-wrapped and dies at the statement; q aliases slot.
    __println("6: pointer-valued rhs");
    {
        int slot = 5;
        int^ q = ptr_rhs(Class(60), ^slot);
        __println("6 end q=" + q^);                // dtor 61 BEFORE this
    }

    // 7: guard-negative BOUNDARY -- a CLASS-valued rhs (built in place) does NOT
    //    seq-wrap its arg temp, so the temp keeps ENCLOSING-scope lifetime: its
    //    dtor (81) runs at the BLOCK end, AFTER "7 end", together with y (181, in
    //    reverse). Pins the current deferred behavior; a future class-rhs fix will
    //    move dtor 81 up to the statement.
    __println("7: class-valued rhs (temp leaks to enclosing scope)");
    {
        Class y = make(Class(80));
        __println("7 end y=" + y.a_);              // dtor 81 does NOT run yet
    }                                              // block end: dtor 181, dtor 81

    // 8: augmented assignment on a scalar -- lowers to a plain assign before the
    //    lift pass, so its arg temp is seq-wrapped too and dies at the statement.
    __println("8: aug-assign rhs");
    {
        int acc = 0;
        acc += fn(Class(90));
        __println("8 end acc=" + acc);             // dtor 91 BEFORE this
    }

    return 0;
}
