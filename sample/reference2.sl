int32 add(int32 a, int32 b) {
    return a + b;
}

void increment(int^ p) {
    p^ = p^ + 1;
}

void swap(int^ a, int^ b) {
    int tmp = a^;
    a^ = b^;
    b^ = tmp;
}

int32 main() {
    int x = 10;
    int y = 20;

    // basic ref
    int^ p = ^x;
    p^ = 42;
    __println("expected: 42");
    __println(x);      // 42

    // pass by ref
    increment(^x);
    __println("expected: 43");
    __println(x);      // 43

    // swap via refs
    swap(^x, ^y);
    __println("expected: 20");
    __println(x);      // 20
    __println("expected: 43");
    __println(y);      // 43

    return 0;
}
