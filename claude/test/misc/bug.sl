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

Class(int a_) {
    _() { __println("Class:ctor: " + a_); }
    ~() { __println("Class:dtor: " + a_); }
    void inc() {
        ++a_;
        __println("Class:inc: " + a_);
    }
    op=(int a) {
        a_ = a;
        __println("Class:op=int: " + a_);
    }
    op+=(int b) {
        a_ += b;
        __println("Class:op+=int: " + a_);
    }
}

global Global (
    Class g,
    Class arr[3]
) {
    _() { __println("Global:ctor: " + g.a_); }
    ~() { __println("Global:dtor: " + g.a_); }
}

int fn(Class^ cls) {
    cls^.inc();
    return cls^.a_;
}

int32 main() {

    //Class cls = Class + 1 + 2 + 3;
    //__println(cls.a_);

    return 0;
}
