/*
define how class templates work
within a single source file.
*/

Value(
    int x_
) {
    _() {
        __println("Value:ctor");
    }
    ~() {
        __println("Value:dtor");
    }
}

Vector<T>(
    intptr size_ = 0,
    T[] storage_ = nullptr
) {
    _() {
        __println("Vector:ctor");
    }
    ~() {
        __println("Vector:dtor");
        delete storage_;
    }

    void resize(intptr new_size) {
        __println("resize: " + new_size);
        delete storage_;
        size_ = new_size;
        storage_ = new T[new_size];
    }
}

int32 main() {
    {
        __println("Vector<int>:");
        Vector<int> vint;
        vint.resize(10);
    }

    {
        __println("Vector<Value>:");
        Vector<Value> vval;
        vval.resize(3);
    }

    return 0;
}
