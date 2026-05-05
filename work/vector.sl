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
        destruct(0, size_);
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
        if (new_size <= capacity_) {
            return;
        }

        /* allocate more storage. */
        byte_size = sizeof(T) * new_size;
        new_storage = new int8[byte_size];

        /* move exising elements. */
        for (
            intptr i = 0,
            T[] src = <T[]> storage_,
            T[] dst = <T[]> new_storage
        ) (i < size_) {
            ++i; ++src; ++dst;
        } {
            dst^ <- src^;
        }

        /* free old storage. */
        delete storage_;

        /* update. */
        capacity_ = new_size;
        storage_ = new_storage;
    }

    /*
    change the number of elements.
    preserve existing elements.
    */
    void resize(intptr new_size) {
        grow(new_size);
        shrink(new_size);
    }

/* private interface. */

    /* add more elements. */
    void grow(intptr new_size) {
        if (new_size <= size_) {
            return;
        }

        /* reserve space. */
        reserve(new_size);

        /* create new elements. */
        construct(size_, new_size);

        /* update. */
        size_ = new_size;
    }

    /* remove elements. */
    void shrink(intptr new_size) {
        if (new_size >= size_) {
            return;
        }

        /* destruct old elements. */
        destruct(new_size, size_);

        /* update. */
        size_ = new_size;
    }

    /* construct new elements */
    void construct(
        intptr begin,
        intptr end
    ) {
        for (
            intptr i = begin,
            int8[] ptr = <int8[]> storage_ + sizeof(T) * i
        ) (i < end) {
            ++i; ptr += sizeof(T);
        } {
            //__println("Vector<T>: placement new.");
            new(ptr) T;
        }
    }

    /* destruct elements. */
    void destruct(
        intptr begin,
        intptr end
    ) {
        for (
            intptr i = begin,
            T[] ptr = <T[]> storage_ + 1
        ) (i < end) {
            ++i; ++ptr;
        } {
            //__println("Vector<T>: inline dtor.");
            ptr^.~();
        }
    }
}
