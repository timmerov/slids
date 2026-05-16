/*
change the way __println works.
*/

int fortytwo() {
    __println("Forty-two!");
    return 42;
}

void dump( (char[], char[], int^)^ tuple) {
    __println(tuple^[0] + " " + tuple^[1] + " = " + tuple^[2]^);
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
