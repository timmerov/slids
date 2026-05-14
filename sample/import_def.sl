/*
implement what's declared in import_decl.slh.
exercises a lazy namespace global: state ctor/dtor live in this TU,
the dtor must still fire when main's `global;` scope closes in the
other TU.
*/

import import_decl;

global state(
    hellos_ = 0,
    goodbyes_ = 0
) {
    _() {
        __println("[state ctor]");
    }
    ~() {
        __println("[state dtor]");
        __println("  hellos: " + hellos_);
        __println("  goodbyes: " + goodbyes_);
    }
}

void printHelloWorld() {
    state:hellos_ = state:hellos_ + 1;
    __println("Hello, World!");
}

void printGoodbye() {
    state:goodbyes_ = state:goodbyes_ + 1;
    __println("Goodbye.");
}
