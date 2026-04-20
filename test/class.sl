
Simple(
    int x_
) {
    _() {
        __println("ctor");
    }
    ~() {
        __println("dtor");
    }
}

Simple {
    void hello() {
        __println("Hello, World!");
    }
}

void Simple:goodbye() {
    __println("Goodbye, World!");
}

int32 main() {
    Simple simple;
    simple.hello();
    simple.goodbye();
    return 0;
}
