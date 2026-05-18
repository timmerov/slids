/*
solitary class instantiation.
*/

Solitary(int x_ = 1, int y_ = 2) {
    _() {
        __println("Solitary:ctor: x=" + x_ + " y=" + y_);
    }
    ~() {
        __println("Solitary:dtor");
    }
}

int32 main() {

    /* currently this is valid syntax. */
    Solitary;

    /* these should be too. */
    /*
    Solitary();
    Solitary(3);
    Solitary(4, 5);
    */

    return 0;
}
