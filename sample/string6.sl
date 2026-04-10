
String(
    int size_ = 0,
    int capacity_ = 0,
    char[] storage_ = nullptr
) {
    _();
    ~();

    void clear();
    void reserve(int cap);
    void set(char[] s);
}

String:_() {
}

String:~() {
    delete storage_;
}

void String:clear() {
    size_ = 0;
}

void String:reserve(int cap) {
    if (cap <= capacity_) {
        return;
    }
    capacity_ = cap;
    delete storage_;
    storage_ = new char[cap];
}

void String:set(char[] s) {
    int len = strlen(s);
    reserve(len);
    size_ = len;
    char[] src = s;
    char[] dst = storage_;
    while (len > 0) {
        dst++^ = src++^;
        --len;
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
    char[] hello_world = "Hello, World!";
    String s;
    s.set(hello_world);
    println("expected: Hello, World!");
    println(s.storage_[0..s.size_]);
    return 0;
}
