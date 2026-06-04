/*
test iterators.

iterators are a type of pointer.
they point to an iterable sequence of objects.
iterators may be assigned, re-assigned, and set to nullptr.
you may do additive math operations on iterators.

square brackets [] after a type indicates an iterator.
caret ^ after an iterator type variable is the object.
caret ^ before a variable takes the address of the variable.

    int[] iter = nullptr;
    int x = 0;
    iter = ^x;
    int y = ^iter;

    char[] str = "Hello, World!";
    char[] e = ^str[1];
    char[] r = e + 8;
    ++e;
    --r;
    intptr diff = r - e;

iterators are incremented by the size of the object.
the difference between to iterators is the difference between
the addresses divided by the size of the object.
*/

/*
claude says:

tbd
*/

int32 main() {

    return 0;
}
