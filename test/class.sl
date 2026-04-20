/*
test adding methods to classes after the fact.
also test forward declarations.
*/

Simple(
    int x_
) {
    _() {
        __println("Simple:ctor");
    }
    ~() {
        __println("Simple:dtor");
    }
}

/*
two valid syntaxes for forward declarations.
*/
void foo();
void Simple:hello();
Simple {
    void goodbye();
}

/*
two valid syntaxes for after-the-fact definitions.
*/
void foo() {
    __println("foo");
}
Simple {
    void hello() {
        __println("Hello, World!");
    }
}
void Simple:goodbye() {
    __println("Goodbye, World!");
}

/*
*/
int32 main() {
    Simple simple;
    simple.hello();
    foo();
    simple.goodbye();
    return 0;
}
