/*
test classes with methods but no fields.

empty classes may be instantiated.
they need to have a minimum size.
so they can be distinguished from each other.
*/

/*
claude says:

tbd
*/

Empty() {
    _() { __println("Empty:ctor"); }
    ~() { __println("Empty:dtor"); }
}

int32 main() {

    Empty a;
    Empty b;
    intptr p = ^a;
    intptr q = ^b;
    __println("same address: " + (p==q));

    return 0;
}
