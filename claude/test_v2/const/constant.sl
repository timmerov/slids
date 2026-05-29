/*
test constants at global and function scope.

the resolve stage must push frames.
and add constants to the frames.

we must be able to fold the rhs constant expression.
then we must be able to use the named constant
to fold other constant expressions.
*/

/*
const float kPi = 3.14;
const int kFortyTwo = 42;

int32 foo() {
    const float kTau = 2.0 * kPi;
    const int kForty = kFortyTwo - 2;
    __println("kTau = " + kTau);
    __println("kForty = " + kForty);
    return 0;
}

int32 bar() {
    foo();
}
*/

int32 main() {
/*
    /* compile errors */
    __println("kTau = " + kTau);
    __println("kForty = " + kForty);

    bar();

    const float kPi2 = kPi / 2.0;
    const int kFortyFour = kFortyTwo + 2;
    __println("kPi2 = " + kPi2);
    __println("kFortyFour = " + kFortyFour);
*/
    return 0;
}
