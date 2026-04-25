
void dump( (char[], char[], int)^ tuple ) {
    __println(tuple^[0] + " " + tuple^[1] + "=" + tuple^[2]);
}

int32 main() {
    int x = 42;
    tuple = ("int", "x", x);
    dump(tuple);
    return 0;
}
