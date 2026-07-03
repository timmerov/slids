/*
test functions.
*/

int32 add(int32 a, int32 b = 1) {
    return a + b;
}

int32 main() {
    __println(add(3, 4));
    __println(add(9));
    return 0;
}
