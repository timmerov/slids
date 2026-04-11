
/* String class. */
String(
    /* length of stored string. */
    int size_ = 0,

    /* maximum size storable. */
    int capacity_ = 0,

    /* pointer to stored string. */
    char[] storage_ = nullptr
) {
    /* constructor/destructor */
    _();
    ~();

    /* set to empty string. */
    void clear();

    /* increase capacity. */
    void reserve(int capacity);

    /* set to a null terminated string. */
    void set(char[] s);

    /* append a character. */
    void append(char c);
}

/* constructor */
String:_() {
}

/* destructor */
String:~() {
    delete storage_;
}

/* block definitions. */
String {
    /* set to empty string. */
    void clear() {
        size_ = 0;
    }

    /* increase capacity. save existing string. */
    void reserve(int cap) {
        if (cap <= capacity_) {
            return;
        }

        char[] new_storage = new char[cap];

        char[] src = storage_;
        char[] dst = new_storage;
        int sz = size_;
        while (sz > 0) {
            dst++^ = src++^;
            --sz;
        }

        capacity_ = cap;
        delete storage_;
        storage_ = new_storage;
    }

    /* set to a null terminated string. */
    void set(char[] s) {
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

    /* append a character. */
    void append(char c) {
        expand(size_ + 1);
        storage_[size_++] = c;
    }

    /*
    private api.
    expect the string to grow.
    add more capacity than requested.
    */
    void expand(int cap) {
        if (cap > capacity_) {
            cap += cap / 2;
        }
        reserve(cap);
    }
}

/* return length of null terminated string. */
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

/* print hello, world! */
int32 main() {
    char[] hello_world = "Hello, World";
    String s;
    s.set(hello_world);
    s.append('!');
    __println("expected: Hello, World!");
    __println(s.storage_[0..s.size_]);
    __println("expected: capacity=19");
    __println("capacity=" + s.capacity_);
    return 0;
}
