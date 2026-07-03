/*
dump prints the type name and value of a variable.
uses the stringification operator #.
*/

/* header: template declaration */
void dump<T>( (char[], char[], char[], char[], T^)^ tuple );

/* source: template definition */
void dump<T>( (char[], char[], char[], char[], T^)^ tuple ) {
    __println(tuple^[2] + " " + tuple^[3] + "=" + tuple^[4]^);
}

/* sample usage. */
int32 main() {
    int x = 42;
    dump(#x);

    return 0;
}
