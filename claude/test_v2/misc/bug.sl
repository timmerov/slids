/*
resolve the bug of the day.
*/

Space {
    enum Dir ( n, s );
    const int a = Dir:n;
    const Dir b = Dir:s;
}

int32 main() {

    int y = Space:Dir:n;
    Space:Dir x = Space:Dir:s;

    return 0;
}
