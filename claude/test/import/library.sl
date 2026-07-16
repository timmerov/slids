/*
test import and linked files.
this is the library source file.
it must define all of the things declared in the header file.
*/

/*
claude says:

tbd
*/

import library;

void hello_world() {
    __println("Hello, World!");
}

Animal() {
    _() {
        __println("Animal:ctor: " + a + " " + b);
    }
    ~() {
        __println("Animal:dtor: " + a + " " + b);
    }
}

void Animal:print() {
    __println("Animal:print: " + a + " " + b);
}

Space {
    Vegetable() {
        _() {
            __println("Vegetable:ctor: " + a + " " + b);
        }
    }
    Vegetable:~() {
        __println("Vegetable:dtor: " + a + " " + b);
    }
}

void Space:Vegetable:print() {
    __println("Vegetable:print: " + a + " " + b);
}

/*
in a source file, we cannot add a ctor or dtor to a class
first declared in a header file.
*/
NoCtor() {
    _() { __println("compile error."); }
}
NoCtor() {
    ~() { __println("compile error."); }
}
