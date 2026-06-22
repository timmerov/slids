/*
test nameless class instantiation.

the author can instantiate a class object without a name.
there are two forms.

statement or local variable form:

    Class(1,2,3);
    Class(1,2,3).method();

like a named class, the unnamed variable object is initialized at site
and the ctor is called.
the dtor is called at end of scope.

expression or temporary variable form:

    x = Class(1,2,3).x_;
    y = Class(1,2,3).method();

like a temporary object, the unnamed temporary object is initialized at site
and the ctor is called.
the dtor is called at end of the statement containing the expression.
in this case, at the semicolon.

the class of an unnamed object must have ctor/dtor defined - either
explicitly or synthesized.
otherwise it's a compile error.
*/

/*
claude says:

tbd
*/

Class(int c_) {
    _() {
        __println("Class:ctor: " + c_);
    }
    ~() {
        __println("Class:dtor: " + c_);
    }
}

int32 main() {

    __println("expect ctor 1,2 after");
    {
        Class(1);
        __println("expect ctor 1 above and ctor 2 after.");
        Class(2);
        __println("expect dtor 1,2 after.");
    }
    __println("expect dtor 2,1 before");

    return 0;
}
