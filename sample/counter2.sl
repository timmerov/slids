
Counter(int32 value_ = 0) {
    void increment() {
        ++value_;
    }
    int32 getValue() {
        return value_;
    }
}

int32 main() {
    Counter c;
    c.increment();
    __println("expected: 1");
    __println(c.getValue());

    Counter c5(5);
    c5.increment();
    __println("expected: 6");
    __println(c5.getValue());

    return 0;
}
