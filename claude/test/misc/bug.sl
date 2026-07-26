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

TClass<T>() {
    Hoisted(int a) { }
}

//T TClass<T>:method(T t) { return t; }

int32 main() {

    TClass<int>:Hoisted obj; obj;

    return 0;
}
