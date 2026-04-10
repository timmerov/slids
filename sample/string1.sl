
String(
    int size_ = 0,
    int capacity_ = 0,
    char[] storage_ = nullptr
) {
    _() {
    }

    ~() {
        delete storage_;
    }

    void reserve(int cap) {
        capacity_ = cap;
        storage_ = new char[cap];
        return;
    }
}

int32 main() {
    String s;
    s.reserve(100);
    println("expected: size=0 capacity=100");
    println("size=" + s.size_ + " capacity=" + s.capacity_);
    return 0;
}
