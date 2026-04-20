
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

    one = new Simple;
    delete one;

    many = new Simple[3];
    delete many;

    return 0;
}
