/*
operator overload resolution by literal type.

a binary op-method with both an int64 and a float64 overload must
dispatch an integer literal to the int64 overload and a float literal
to the float64 overload. a float literal must never bind to the
int64 parameter — doing so would emit a float constant for an
integer operand.
*/

Num(int v_ = 0) {
    _() {
    }
    ~() {
    }

    op*(Num^ n, int64 x) {
        __println("int64");
    }

    op*(Num^ n, float64 x) {
        __println("float64");
    }
}

int32 main() {
    Num n;
    Num r;
    r = n * 7;      /* integer literal -> op*(Num^, int64)   */
    r = n * 3.5;    /* float literal   -> op*(Num^, float64) */
    return 0;
}
