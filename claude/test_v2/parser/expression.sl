/*
test implementation of expressions.

unary: + - ~ !
binary:
    math: + - * / %
    bitwise: & | ^ << >>
    logical: && || ^^
    comparison: == != >= <= > <

precedence, parentheses, ...

! and comparisons consume condition-expressions.
0-like values (false, 0, 0.0, nullptr) are false.
everything else is true.

pre/post increment/decrement not tested here.
the ppid rule has implications for parameters.

augmented assignments: += -= *= /= %= &= |= ^= <<= >>= &&= ||= ^^=

augmented assignements are handled in the desugar stage.
*/

int32 main() {

    int u = 5;
    int pu = +u;
    int nu = -u;
    int cu = ~u;
    bool bu = !u;
    __println("pu= " + pu);
    __println("nu= " + nu);
    __println("cu= " + cu);
    __println("bu= " + bu);

    int p5 = +5;
    int n5 = -5;
    int c5 = ~5;
    bool b5 = !5;
    __println("p5= " + p5);
    __println("n5= " + n5);
    __println("c5= " + c5);
    __println("b5= " + b5);

    return 0;
}
