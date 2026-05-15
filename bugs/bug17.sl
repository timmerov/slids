/*
change the way __println works.
*/

int fortytwo() {
    __println("Forty-two!");
    return 42;
}

int32 main() {
    __println("fortytwo = " + fortytwo());
    return 0;
}
