/*
optimize the evaluation of expressions that need temporary class objects.

    String s = String + a + b + c + d

if the String class defines op= for a's type and op+= for b,c,d's types
then that entire variable declaration statement can be evalutated with
zero temporary String objects.

    s = a; s += b; s += c; s += d;

in cases where lhs is existing object variable and rhs is a temp,
move (not copy) semantics should be used when possible.

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

tbd
*/

/*
Class(int a_) {
    _() { __println("Class:ctor: " + a_); }
    ~() { __println("Class:dtor: " + a_); }
    op=(Class^ rhs) {
        __println("Class:op=: " + a_ + " = " + rhs^.a_);
        a_ = rhs^.a_;
    }
    op+=(Class^ rhs) {
        __println("Class:op+=: " + a_ + " += " + rhs^.a_);
        a_ += rhs^.a_;
    }
    op+(Class^ lhs, Class^ rhs) {
        __println("Class:op+: " + a_ + " = " + lhs^.a_ + " + " + rhs^.a_);
        a_ = lhs^.a_ + rhs^.a_;
    }
}
*/

int32 main() {

/*
    Class ca = 5;
    Class cb = 7;
    Class cc = 9;
    /*
    if any temporaries are created to evaluate this expression
    then their dtors must run at the end of the phrase.
    which is the statement.
    however, we should be able to do this with no temporaries.
    */
    __println("before assignment expression");
    Class cd = ca + cb + cc;
    __println("after assignment expression");
    __println("cd: " + cd.a_);
*/

    return 0;
}
