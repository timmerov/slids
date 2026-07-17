/*
test import and linked files.
this is the library source file.
it must define all of the things declared in the header file.

in a source file, we cannot add a ctor/dtor or copy, move, swap
operator to a class first declared in a header file.
*/

/*
claude says:

this is library.slh's SIBLING — same base name (the directories would be allowed to
differ). that is what makes this file, and no other, responsible for emitting the
SYNTHESIZED members of every class the header declares: the complete ctor/dtor and the
default copy/move/swap. consumer.sl only DECLARES those and links here.

the split is between synthesized and declared, not between this file and the rest:
  - SYNTHESIZED (nobody wrote it) -> only the sibling can emit it. no source's location
    could select an owner, so the rule has to be positional.
  - DECLARED (a method, an operator, a hook body) -> definable in ANY ONE .sl. a header
    declaring several classes is meant to be able to give each one its own source file.
so everything defined below is here because it is convenient, not because it is forced.
defining it zero times or twice is a LINK error — this compiler sees one TU and cannot
know what the others define. that is the same deal hello_world() already gets.

the external `Vegetable:~()` and the block re-open `Vegetable() { _() {...} }` are two
spellings of the same thing, on purpose: a ctor/dtor is a method with restrictions, so
if a syntax works for a method it has to work here.

KNOWN GAP (todo: HOOK BODY IN A NON-SIBLING TU): a header-declared hook whose body sits
in a non-sibling .sl mis-compiles today — this file would emit the complete `@C__$ctor`
calling a `@C__$ctor__impl` it never declares, and llc rejects it with no diagnostic
from slidsc. so the hook bodies below are in the sibling because that is what WORKS, not
because the model requires it.
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
