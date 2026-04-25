
void dump1( (char[], char[], int)^ tuple ) {
    __println(tuple^[0] + " " + tuple^[1] + "=" + tuple^[2]);
}

void dump2<T>( T^ tuple ) {
    __println(tuple^[0] + " " + tuple^[1] + "=" + tuple^[2]);
}

void dump3<T>( (char[], char[], T)^ tuple ) {
    __println(tuple^[0] + " " + tuple^[1] + "=" + tuple^[2]);
}

int32 main() {
    int x = 42;
    tuple = ("int", "x", x);
    dump1(tuple);
    dump2(tuple);
    dump3(tuple);
    return 0;
}
