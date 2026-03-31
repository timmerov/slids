Counter(int32 value_ = 0) {
    void increment() {
        value_ += 1;
    }
    int32 getValue() {
        return value_;
    }
}

int32 main() {
    Counter c;
    c.increment();
    println("expected: 1");
    println(c.getValue());

    Counter c5(5);
    c5.increment();
    println("expected: 6");
    println(c5.getValue());

    return 0;
}
