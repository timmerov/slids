
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

    /* set to a null-terminated string. */
    void set(char[] s);

    /* append an array of characters. */
    void append(char[] arr, int sz);

    /* append a character. */
    void append(char c);

    /* append a null-terminated string. */
    void append(char[] s);
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
        copy_chars(new_storage, storage_, size_);

        capacity_ = cap;
        delete storage_;
        storage_ = new_storage;
    }

    /* set to a null terminated string. */
    void set(char[] s) {
        int len = strlen(s);
        reserve(len);
        size_ = len;
        copy_chars(storage_, s, len);
    }

    /* append a character. */
    void append(char[] arr, int sz) {
        expand(size_ + sz);
        copy_chars(storage_ + size_, arr, sz);
        size_ += sz;
    }

    /* append a character. */
    void append(char c) {
        append(^c, 1);
    }

    /* append a null-terminated string. */
    void append(char[] s) {
        int len = strlen(s);
        append(s, len);
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

/* copy count characters from src to dst. */
void copy_chars(
    char[] dst,
    char[] src,
    int count
) {
    while (count > 0) {
        dst++^ = src++^;
        --count;
    }
}

/* print hello, world! */
int32 main() {
    char[] hello = "Hello";
    char[] world = "World";
    String s;
    s.set(hello);
    s.append(", ");
    s.append(world);
    s.append('!');
    println("expected: Hello, World!");
    println(s.storage_[0..s.size_]);
    return 0;
}
