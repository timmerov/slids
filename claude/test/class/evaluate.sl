/*
optimize the evaluation of expressions that need temporary class objects.

    String s = String + a + b + c + d

if the String class defines op= for a's type and op+= for b,c,d's types
then that entire variable declaration statement can be evalutated with
zero temporary String objects.

    s = a; s += b; s += c; s += d;


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
