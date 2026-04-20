/*
define a vector class declared in a header file.
*/

import vector;

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
