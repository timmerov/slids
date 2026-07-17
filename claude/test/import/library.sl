/*
test import and linked files.
this is the library source file.
it must define all of the things declared in the header file.

in a source file, we cannot add a ctor/dtor or copy, move, swap
operator to a class first declared in a header file.
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
    op=(Animal^ rhs) {
        a = rhs^.a;
        b = rhs^.b;
        __println("Animal:op=: " + a + " " + b);
    }
}

void Animal:print() {
    __println("Animal:print: " + a + " " + b);
}

Space {
    void goodbye_world() {
        __println("Goodbye, World!");
    }

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
in a source file, we cannot add a ctor/dtor or copy, move, swap operator to a class
first declared in a header file. these five are IMPLICITLY invoked, so every importing
TU emits calls to them from the header alone — adding one here would make this TU
disagree with every other about what constructing or copying a NoCtor does, silently.
the ban is on ADDING: Animal's hooks and op= ARE declared in the header, so defining
them (above) is legal. this file is the sibling, and it is still not allowed.
*/
//-EXPECT-ERROR: cannot add a constructor
//NoCtor:_() { __println("compile error."); }

//-EXPECT-ERROR: cannot add a destructor
//NoCtor:~() { __println("compile error."); }

//-EXPECT-ERROR: cannot add a copy operator
//NoCtor:op=(NoCtor^ rhs) { __println("compile error."); }

//-EXPECT-ERROR: cannot add a move operator
//NoCtor:op<--(mutable NoCtor^ rhs) { __println("compile error."); }

//-EXPECT-ERROR: cannot add a swap operator
//NoCtor:op<-->(mutable NoCtor^ rhs) { __println("compile error."); }

/* the block re-open form is the same violation. */
//-EXPECT-ERROR: cannot add a constructor
//NoCtor() {
//    _() { __println("compile error."); }
//    ~() { __println("compile error."); }
//}
