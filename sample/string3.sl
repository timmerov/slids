
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
        if (cap <= capacity_) {
            return;
        }
        capacity_ = cap;
        delete storage_;
        storage_ = new char[cap];
    }

    void set(char[] s) {
        int len = strlen(s);
        if (capacity_ < len) {
            reserve(len);
        }
        char[] src = s;
        char[] dst = storage_;
        while (len > 0) {
            dst++^ = src++^;
            --len;
        }
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
    char[] null_terminated_string_literal = "Hello, World!";
    String s;
    s.set(null_terminated_string_literal);
    println("expected: size=13 capacity=13");
    println("size=" + s.size_ + " capacity=" + s.capacity_);
    return 0;
}
