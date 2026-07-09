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
}

int32 main() {
/*
    __println("before");
    int x = fn(Class(10));
    x;
    __println("after");
*/
    return 0;
}
