
Simple(
    int x_ = 0,
    int y_ = 0,
    int z_ = 0
) {
    /* constructor/destructor */
    _() {
        __println("constructor");
    }
    ~() {
        __println("destructor");
    }
}

int32 main() {

    __println("expected 1 con 1 des");
    one = new Simple;
    delete one;

    __println("expected 3 con 3 des");
    many = new Simple[3];
    delete many;

    return 0;
}
