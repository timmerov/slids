
int32 add(int32 a, int32 b) {
    return a + b;
}

int32 main() {
    __println("expected: 7");
    __println(add(3, 4));
    return 0;
}
