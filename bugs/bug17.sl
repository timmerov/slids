/*
change the way __println works.
*/

int fortytwo() {
    __println("Forty-two!");
    return 42;
}

int32 main() {
    Class(x_=0) {
    }
    Class cls;
    __println(##name(cls.x_));

    __println("fortytwo = " + fortytwo());
    return 0;
}
