/*
redefine how we find the sizeof an incomplete class.
*/

/* declare the incomplete class. */
Incomplete(...) {
    _();
    ~();
    void greet();

    /*
    this declaration is optional.
    the ... above indicates an incomplete class.
    ergo this function must exist.
    */
    intptr sizeof();
}

/* define the incomplete class. */
Incomplete(
    ...,
    int x_
) {
    _() {
        __println("Incomplete:ctor");
    }

    ~() {
        __println("Incomplete:dtor");
    }

    void greet() {
        __println("Hello, World!");
    }

    /* the compiler creates this function. */
    intptr sizeof() = auto;
}

int32 main() {
    Incomplete inc;
    inc.greet();

    /* builtin sizeof variable */
    size = sizeof(inc);
    __println("sizeof(inc)<4>=" + size);

    /* builtin sizeof type */
    size = sizeof(Incomplete);
    __println("sizeof(Incomplete)<4>=" + size);

    /* sizeof by class method */
    size = inc.sizeof();
    __println("inc.sizeof()<4>=" + size);

    /* sizeof by type */
    size = Incomplete.sizeof();
    __println("Incomplete.sizeof()<4>=" _ size);

    return 0;
}
