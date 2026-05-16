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

    /*
    assignment from Vector.
    copy operator.
    rhs may be self.
    */
    op=(Vector<T>^ v) {
        /* don't copy self. */
        if (v == ^self) {
            return;
        }

        /* make space. */
        resize(v^.size_);

        /* copy elements. */
        for (
            intptr i = 0,
            T[] src = <T[]> v^.storage_,
            T[] dst = <T[]> storage_
        ) (i < size_) {
            ++i; ++src; ++dst;
        } {
            dst^ = src^;
        }
    }

    /*
    move operator.
    rhs may be self.
    */
    op<--(mutable Vector<T>^ v) {
        /* don't move self. */
        if (v == ^self) {
            return;
        }

        /* delete our storage. */
        delete storage_;

        /* copy from v. */
        size_ = v^.size_;
        capacity_ = v^.capacity_;
        storage_ = v^.storage_;

        /* clear v. */
        v^.size_ = 0;
        v^.capacity_ = 0;
        v^.storage_ = nullptr;
    }

    /*
    overload index operation to fetch/set the indexed element.
    T elem = vec[3];
    vec[3] = elem;
    */
    T^ const op[](intptr index) {
        T[] ptr = <T[]> storage_;
        return ptr + index;
    }

    /* return the number of elements. */
    intptr const size() {
        return size_;
    }

    /* begin iterator */
    T^ const begin() {
        return <T[]> storage_;
    }

    /* end iterator */
    T^ const end() {
        ptr = <T[]> storage_;
        return ptr + size_;
    }

    /* update iterator */
    T^ const next(T^ prev) {
        /*
        reinterpret the refernce to const:
            (const char)^ prev
        as a mutable iterator.
            char[] iter
        */
        iter = <T[]> <mutable> prev;
        return iter + 1;
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
            dst^ <-- src^;
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

    /* add the element at the end. */
    void append(T^ element) {
        insert(size_, element);
    }

    /*
    create an element at the end.
    return a reference to it.
    */
    T^ append() {
        return insert(size_);
    }

    /* insert the element at the index. */
    void insert(intptr index, T^ element) {
        slot = insert(index);
        slot^ = element^;
    }

    /*
    insert the element at the index.
    return a reference to it.
    */
    T^ insert(intptr index) {
        grow(size_ + 1);

        /* move existing elements */
        for (
            intptr i = size_ - 1
        ) (i > index) {
            --i;
        } {
            self[i] <-- self[i-1];
        }

        return ^self[index];
    }

/* private interface. */

    /* add more elements. */
    void grow(intptr new_size) {
        if (new_size <= size_) {
            return;
        }

        /* reserve extra space. */
        new_size += new_size / 2;
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
