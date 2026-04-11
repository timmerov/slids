
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
    _() {
        __println("size=" + size_ + " capacity=" + capacity_ + " storage_=" + storage_[0..size_]);
    }
    ~();
}

/*
test program.
*/
int32 main() {
    String s1;
    String s2();
    String s3(1);
    String s4(2, 3);
    String s5(4, 5, "Hello, World!");
    return 0;
}
