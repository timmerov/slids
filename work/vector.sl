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
    reserve space for more elements.
    preserve existing elements.
    */
    void reserve(intptr new_size) {

        /* already have enough room. */
        if (new_size >= capacity_) {
            return;
        }

        /* allocate more storage. */
        new_storage = new T[new_size];
        /* move exising elements. */
        intptr i = 0;
        T[] src = storage_;
        T[] dst = new_storage;
        while (i++ < size_) {
            /* meh. not supported. */
            //dst++^ <- src++^;
        }

        /* free old storage. */
        delete storage_;

        /* update. */
        size_ = new_size;
        storage_ = new_storage;
    }

    /*
    change the number of elements.
    preserve existing elements.
    */
    void resize(intptr new_size) {/*

        /* add more elements. */
        if (new_size > size_) {
            /* reserve space. */
            reserve(new_size);

            /* create new elements. */
            intptr i = size_;
            T[] ptr = storage_ + i;
            while (i++ < new_size) {
                new(ptr++) T;
            }
        } else {
            /* destruct old elements. */
            intptr i = new_size;
            T[] ptr = storage_ + i;
            while (i++ < size_) {
                ptr++^.~();
            }
        }
        /* update. */
        size_ = new_size;
    */}
}
