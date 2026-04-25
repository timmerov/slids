/*
dump prints the type name and value of a variable.
uses the stringification operator #.
*/

/* header: template declaration */
void dump<T>( (char[], char[], T)^ tuple );

/* source: template definition */
void dump<T>( (char[], char[], T)^ tuple ) {
    __println(tuple^[0] + " " + tuple^[1] + "=" + tuple^[2]);
}

/* sample usage. */
int32 main() {
    int x = 42;
    dump(#x);

    return 0;
}
