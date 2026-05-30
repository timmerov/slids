/*
test aliases.

alias Name = type;

todo:

drop qualifiers:
    alias Namespace;

rename functions:
    alias Fn = LongOrWrongFunctionName;
use case is importing c functions with stupid names.

##type returns the alias.
Name x;
##type(x);
*/

alias Integer = int;
alias Float = float;
alias Whole = Integer;        // chained to another alias

int32 doubled(Integer n) {    // alias in param + return position
    return n + n;
}

int32 main() {

    Integer x = 42;
    __println("x = " + x);

    Float y = 3.14;
    __println("y = " + y);

    Whole z = 7;              // Whole -> Integer -> int
    __println("z = " + z);

    Integer w;               // declare-then-assign through an alias type
    w = doubled(20);
    __println("w = " + w);

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
