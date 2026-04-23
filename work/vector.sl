/*
define a vector class declared in a header file.
*/

import vector;

Vector<T>(
    ...,
    intptr size_ = 0,
    intptr capacity_ = 0,
    T[] storage_ = nullptr
) {
    _() {
    }
    ~() {
        delete storage_;
    }

    /* return the number of elements. */
    intptr size() {
        return size_;
    }

    /*
    change the number of elements.
    preserve existing elements.
    */
    void resize(intptr new_size) {
        //reserve(new_size);
        delete storage_;
        size_ = new_size;
        storage_ = new T[new_size];
    }
}
