/*
namespaces are slids with no data block.
slids are namespaces.
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
}

/* slid class: Class c; c.greet() */
Class() {
    void greet() {
        __println("Class:greet: hello");
        Space:greet();
        ::greet();
    }

    void other() {
        /* calls Class:greet() */
        greet();
    }
}

/* slid class as namespace. c.greet2() */
Class {
    void greet2() {
        __println("Class:greet2: good day.");
        Space:greet();
        ::greet();
    }

    void other2() {
        /* calls Class:greet2() */
        greet2();
    }
}

int32 main() {

    Class c;

    greet();
    ::greet();
    Space:greet();
    c.greet();
    c.greet2();

    other();
    ::other();
    Space:other();
    c.other();
    c.other2();

    return 0;
}
