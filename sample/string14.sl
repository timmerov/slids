
/* String class declaration. */
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

    /* set to an array of characters. */
    void set(char[] arr, int sz);

    /* set to a null-terminated string. */
    void set(char[] s);

    /* append an array of characters. */
    void append(char[] arr, int sz);

    /* append a character. */
    void append(char c);

    /* append a null-terminated string. */
    void append(char[] s);

    /* append another string. */
    void append(String^ s);

    /* print the string. */
    void print();
    void println();

    /* assignment from null terminated string. */
    void op=(char[] s);

    /* assignment from String. */
    void op=(String^ s);

    /* move operator. */
    void op<-(String^ s);
}

/* overload + to concatenate strings. */
String op+(String^ sa, String^ sb);


/* String class implementation. */

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

    /* set to an array of characters. */
    void set(char[] arr, int sz) {
        reserve(sz);
        size_ = sz;
        copy_chars(storage_, arr, sz);
    }

    /* set to a null terminated string. */
    void set(char[] s) {
        int len = strlen(s);
        set(s, len);
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

    /* append another string. */
    void append(String^ s) {
        append(s^.storage_, s^.size_);
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

    /* print the string. */
    void print() {
        __print(storage_[0..size_]);
    }

    /** print the string on a line. */
    void println() {
        __println(storage_[0..size_]);
    }

    /* assignment from null terminated string. */
    void op=(char[] s) {
        set(s);
    }

    /* assignment from String. */
    void op=(String^ s) {
        set(s^.storage_, s^.size_);
    }

    /* move operator. */
    void op<-(String^ s) {
        delete storage_;
        size_ = s^.size_;
        capacity_ = s^.capacity_;
        storage_ = s^.storage_;
        s^.size_ = 0;
        s^.capacity_ = 0;
        s^.storage_ = nullptr;
    }
}

/* helper functions. */

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

/* concatenate two strings. */
String op+(String^ sa, String^ sb) {
    String sc;
    sc.set(sa^.storage_, sa^.size_);
    sc.append(sb^.storage_, sb^.size_);
    return sc;
}

/*
test program.
print hello, world!
*/
int32 main() {
    String s1 = "Hello" ", " "World" "!";
    __println("expected: Hello, World!");
    s1.println();
    return 0;
}
