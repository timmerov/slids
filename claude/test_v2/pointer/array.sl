/*
test fixed size arrays.

square brackets around a const-expression in a variable declaration indicates
the variable is a fixed size array.
square brackets around a const-expression after an array variable is the lvalue
of the object at that position.
a caret ^ before an indexed array variable is an iterator.

    int arr[5];
    for (int i = 0) (i < 5) { ++i; } {
        arr[i] = i* i;
    }
    int[] iter = ^arr[3];
*/

/*
claude says:

tbd
*/

int32 main() {

    return 0;
}
