/*
change the way __println works.
*/

int fortytwo() {
    __println("Forty-two!");
    return 42;
}

void dump( (char[], char[], char[], char[], int^)^ tuple) {
    __println(tuple^[2] + " " + tuple^[3] + " = " + tuple^[4]^);
}

int32 main() {
    Class(x_=99) {
    }
    Class cls;
    __println(##name(cls.x_));
    dump(#cls.x_);

    __println("fortytwo = " + fortytwo());
    return 0;
}
