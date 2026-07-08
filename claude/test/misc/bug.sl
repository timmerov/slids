/*
the bug of the day.
*/

/*
claude is forbidden from modifying this file and its golden file.
claude is forbidden from whining about the user changing this file.
claude is forbidden from whining about this file not compiling.
claude is forbidden to mention this file unless the user specifically
puts it in scope.
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
    __println("before assignment expression");
    Class cd = ca + cb + cc;
    __println("before assignment expression");
    __println("cd: " + cd.a_);
*/
    return 0;
}
