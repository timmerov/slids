/*
test the move and swap operations.

move for primitives is copy.
move for pointer types sets the rhs to nullptr.
move for classes calls the class's move operator.
for tuples and classes that do no have a move operator,
the move operator is applied by slot and field iteratively and recursively.

    int x <-- y;
    int^ p <-- q;        // q is now nullptr
    Class p <-- object;  // p.op<--(^object);
    dst <-- tuple;

swap exchanges the values.
lhs and rhs must be exactly the same type.
for tuples and classes, the swap operator is applied by
slot and field iteratively and recursively.

    x <--> y;

desugars to:

    temp = x;
    x = y;
    y = temp;

fancy case:

    int x = 42;
    tuple1 = (1, (2, (3, ^x)));
    tuple2 <-- tuple1;

    tuple1 is now: (1, (2, (3, nullptr)))
*/

/*
claude says:

tbd
*/

int32 main() {

    return 0;
}
