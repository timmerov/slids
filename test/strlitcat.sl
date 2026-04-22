/*
test concatenation of string literals.
*/

int32 main() {
    char[] greet =
        "Hello"
        ", "
        "World"
        "!";
    __println("greet<Hello, World!>=" + greet);

    greet = "Hello, \
World!";
    __println("greet<Hello, World!>=" + greet);

    greet = "tab\t newline\n backslash\\ doublequote\"";
    __println("greet<tab\t newline\n backslash\\ doublequote\">=" + greet);

    return 0;
}
