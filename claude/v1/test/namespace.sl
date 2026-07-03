/*
namespaces
*/

/* global unnamed namesapce ::greet() */
void greet() {
    __println("greet: aloha");
}

void other() {
    __println("other: mahalo");
}

/* namespace Space:greet() */
Space {
    void greet() {
        __println("Space:greet: howdy");
        ::greet();
    }

    void other() {
        /* calls Space:greet() */
        greet();
    }

    /* declarations. */
    void greet2();
    void other2();
}

/* inline definition. */
void Space:greet2() {
    __println("Space:greet2: good day.");
    Space:greet();
    ::greet();
}

/* reopen namespace */
Space {
    void other2() {
        /* calls Space:greet2() */
        greet2();
    }
}

int32 main() {

    greet();
    other();
    ::greet();
    ::other();
    Space:greet();
    Space:other();
    Space:greet2();
    Space:other2();

    return 0;
}
