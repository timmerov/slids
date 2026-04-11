Counter(int32 value_ = 0) {
    void increment() {
        value_ += 1;
    }

    int32 getValue() {
        return value_;
    }
}

void incrementTwice(Counter^ c) {
    c^.increment();
    c^.increment();
}

int32 main() {
    Counter c(10);
    incrementTwice(^c);
    __println("expected: 12");
    __println(c.getValue());   // 12
    return 0;
}
