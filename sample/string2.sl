
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

    void clear() {
        size_ = 0;
    }

    void reserve(int cap) {
        capacity_ = cap;
        delete storage_;
        storage_ = new char[cap];
    }

    void set(char[] s) {
        int len = strlen(s);

    }
}

int strlen(char[] s) {
    int len = 0;
    while (true) {
        char ch = s^;
        if (ch == 0) {
            break;
        }
        ++s;
        ++len;
    }
    return len;
}

int32 main() {
    char[] text = "Hello, World!";
    String s;
    s.reserve(100);
    s.set(text);
    println("expected: size=0 capacity=100");
    println("size=" + s.size_ + " capacity=" + s.capacity_);
    return 0;
}
