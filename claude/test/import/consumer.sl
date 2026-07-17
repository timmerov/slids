/*
test import and linked files.
this is the consumer file.

in a source file, we cannot add a ctor/dtor or copy, move, swap
operator to a class first declared in a header file.
*/

/*
claude says:

tbd
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
