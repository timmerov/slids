/*
define a vector class declared in a header file.
*/

import vector;

Vector<T>(
    ...,
    intptr size_ = 0,
    intptr capacity_ = 0,
    int8[] storage_ = nullptr
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
        byte_size = sizeof(T) * new_size;
        new_storage = new int8[byte_size];

        /* move exising elements. */
        intptr i = 0;
        T[] src = <T[]> storage_;
        T[] dst = <T[]> new_storage;
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
    void resize(intptr new_size) {

        /* add more elements. */
        if (new_size > size_) {
            /* reserve space. */
            reserve(new_size);

            /* create new elements. */
            intptr i = size_;
            int8[] ptr = storage_;
            ptr += sizeof(T) * i;
            /*while (i++ < new_size) {
                new(ptr) T;
                ptr += sizeof(T);
            }*/
        } else {
            /* destruct old elements. */
            intptr i = new_size;
            T[] ptr = <T[]> storage_;
            ptr += i;
            while (i++ < size_) {
                ptr^.~();
                ++ptr;
            }
        }
        /* update. */
        size_ = new_size;
    }
}
