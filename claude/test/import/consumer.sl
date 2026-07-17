/*
test import and linked files.
this is the consumer file.

in a source file, we cannot add a ctor/dtor or copy, move, swap
operator to a class first declared in a header file.
*/

/*
claude says:

this file SYNTHESIZES NOTHING. every class it touches is declared in library.slh, so
this TU is not the sibling and emits only `declare`s — the complete ctor/dtor and the
default copy/move/swap it calls are all library.sl's. that is the whole point of the
test: `Animal dog(1,2)` constructs and destructs an object whose ctor body this file
has never seen.

what each line is actually holding down:
  hello_world()             a plain declared function across the seam (the first slice).
  Integer / Color:kRed      an alias + enum from the header. the declaration IS the
                            definition for those, so they cost no linkage at all.
  Animal dog(1,2)           a class across the seam, incl. the implicit dtor call at
                            the end of main. dog.print() links to @Animal__print — and
                            that symbol is why "is this overloaded?" must count DISTINCT
                            SIGNATURES: print's declaration and its definition are two
                            entries, so counting entries made the definer emit
                            @Animal__print.8 while this file called @Animal__print.
  Space:Float / Space:...   namespace-scoped statics.
  Space:goodbye_world()     a namespace MEMBER: it must mangle by SCOPE PATH
                            (@Space__goodbye_world), never by entry id, or this file and
                            library.sl number their entries differently and never link.
  Space:Vegetable peas(3,4) the nested-class case, same seam.
the golden's line ORDER is load-bearing: the two dtors run in reverse declaration order
at the close of main.

the NoCtor negatives below are the same rule as the sibling's, and that is the point —
the ban is about the CLASS being declared in a header, not about which source file this
is. neither file may add one.
*/

import library;

int32 main() {

    hello_world();

    Integer x = Color:kRed; x;
    Animal dog(1,2);
    dog.print();

    Space:Float f = 3.14; f;
    x = Space:Result:kSuccess;
    Space:Vegetable peas(3,4);
    peas.print();

    Space:goodbye_world();

    return 0;
}

/*
in a source file, we cannot add a ctor/dtor or copy, move, swap operator to a class
first declared in a header file. the rule is the same here as in the sibling: it is
about the class being declared in a header, not about which source file this is.
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
