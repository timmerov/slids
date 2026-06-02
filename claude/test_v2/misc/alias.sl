/*
test aliases.

    alias Name = type;

##type returns the alias.
    Name x;
    ##type(x);

todo:

drop qualifiers:
    alias Namespace;

rename functions:
    alias Fn = LongOrWrongFunctionName;
use case is importing c functions with stupid names.
*/

alias Integer = int;
alias Float = float;
alias Whole = Integer;        // chained to another alias

enum Dir ( kNorth, kSouth, kEast, kWest );

int32 doubled(Integer n) {    // alias in param + return position
    return n + n;
}

int32 main() {

    Integer x = 42;
    __println(##type(x) + " " + ##name(x) + " = " + x);

    Float y = 3.14;
    __println(##type(y) + " " + ##name(y) + " = " + y);

    Whole z = 7;              // Whole -> Integer -> int
    __println(##type(z) + " " + ##name(z) + " = " + z);

    Integer w;               // declare-then-assign through an alias type
    w = doubled(20);
    __println(##type(w) + " " + ##name(w) + " = " + w);

    Dir d = Dir:kNorth;
    __println(##type(d) + " " + ##name(d) + " = " + d);

    //-EXPECT-ERROR: Unknown type 'Bogus'
    //Bogus q;

    //-EXPECT-ERROR: is a type, not a value
    //int32 v = Integer;

    //-EXPECT-ERROR: is a type, not a function
    //Integer(5);

    return 0;
}

//-EXPECT-ERROR: Unknown type 'Nope'
//alias Bad = Nope;

//-EXPECT-ERROR: is part of a cycle
//alias Loop1 = Loop2;
//alias Loop2 = Loop1;

//-EXPECT-ERROR: Duplicate declaration of 'Integer'
//alias Integer = float;
