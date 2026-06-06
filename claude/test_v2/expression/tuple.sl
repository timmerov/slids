/*
test anonymous tuples.

a tuple is a comma separated list of types or data.
types can be mixed.
the slots are not named.
tuples are assign-able.
slots can be accessed by constant index.
tuples can be used whenever variables are used.
tuples are initializers to classes and arrays.
tuples may be nested.

    (1, 2, 3)
    ("Hello", 4.5, 7)
    pair = (Dir:kN, false);
    dir = pair[0];
    good = pair[1];
    other = pair;
    Class c = (123, "Maple");
    Class c(456, "Main");
    array[2][3] = ((1,2), (3,4), (5,6));

    ##type(pair) is (Dir, bool)
    (Dir, bool) function( (Dir, bool)^ tuple );
    pair = function(^other);

    #x desugars to:
    (##file, ##line, ##type(x), ##name(x), ^x)

tuples may be destructured into variables.
slots may be empty.

    (dir, result) = pair;
    (dir, ) = pair;

tuples may be operands in math operations.
the operation is applied slot by slot and recurses.
the types stored at each slot must be compatible via widening rules.
the tuples must have the same shape.

    (1,2,3) + (4,5,6) is (5,7,9)

a math operation between a scalar and a tuple applies the math
operation between the scalar and every slot iteratively and recursively.

    (1,2,3) + 7 = (8,9,10)

for loop iterates over a homogeneous tuple.

    for (x : (1,3,4)) { }

notes:

tuples generally must be size 2 or more.
a tuple of size 1 is a scalar.
a scalar is a tuple of size 1.
is this a parenthesized expression?
or a tuple of size 1?
answer: it's both.
they're interchangeable.

    (expr)

arrays are homogenous tuples.
*/

/*
claude says:

tbd
*/

int32 main() {

    return 0;
}
