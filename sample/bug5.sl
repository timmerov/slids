
char[] getGreeting() {
    /*
    this should compile.
    but doesn't.
    */
    return "Hello, World!";
}

int32 main() {
    char[] greeting = getGreeting();
    __println(greeting);
    return 0;
}
