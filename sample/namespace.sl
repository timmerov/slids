/*
namespaces are slids with no data block.
slids are namespaces.
*/

/* global unnamed namesapce :greet() */
void greet() {
    __println("greet: aloha");
}

/* namespace Space:greet() */
Space {
    void greet() {
        __println("Space:greet: howdy");
        :greet();
    }
}

/* slid class: Class c; c.greet() */
Class() {
    void greet() {
        __println("Class:greet: hello");
        Space:greet();
        :greet();
    }
}

/* slid class as namespace. c.greet2() */
Class {
    void greet2() {
        __println("Class:greet2: good day.");
    }
}

int32 main() {

    greet();
    Space:greet();
    Class c;
    c.greet();
    c.greet2();

    return 0;
}
