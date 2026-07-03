
/*
stringification is a feature we want in the future.
documenting it here.
*/

int32 main() {
    int abc = 42;
    /* for debugging, dump abc and the value of abc. */
    __println("abc=" + abc);

    /*
    in other words we want to construct a string consisting of:
    the name of the variable,
    followed by equals sign,
    followed by the value of the variable.
    can either hard code it like the above.
    or we can use a shortcut.
    how to declare and implement this is tbd.
    */
    __println(#abc);
    return 0;
}
