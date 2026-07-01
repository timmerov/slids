/*
the bug of the day.
*/

/*
claude is forbidden from modifying this file
and its golden file.
*/

int32 main() {

    /*
    this correctly errors out with x set but not used.
    */
    //int x = 42;
    /*
    but...
    i'm writing quickie code to test something.
    i don't care that x isn't used.
    i want a fast and easy way to "use" x.
    the obvious:
    */
    //x;
    /*
    is a confusing error message.
    'x' is not a statement; a bare name is a class construction.
    where else is that used?
    i think i want that syntax to be accepted and to "use" x.
    */

    return 0;
}
