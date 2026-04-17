/*
String class implementation.

also included are helper functions for:
concatenating strings,
length of a null terminated string,
copying null terminated strings,
copying null terminated strings limited by length.
*/

transport string;

/* block definitions. */
String (
    /* there are no public fields. */
    ...,

    /* length of stored string. */
    intptr size_ = 0,

    /* maximum size storable. */
    intptr capacity_ = 0,

    /* pointer to stored string. */
    char[] storage_ = nullptr
) {
    /* constructor */
    _() {
    }

    /* destructor */
    ~() {
        delete storage_;
    }

    /* assignment from String. */
    op=(String^ s) {
        /* don't copy ourself. */
        if (s == ^self) {
            return;
        }
        set(s^.storage_, s^.size_);
    }

    /* assignment from null terminated string. */
    op=(char[] str) {
        len = strlen(str);
        set(str, len);
    }

    /* assignment from char. */
    op=(char c) {
        set(^c, 1);
    }

    /* assignment fron int64. */
    op=(int64 x) {
        if (x >= 0) {
            fillDigitsBackwards(x);
        } else {
            fillDigitsBackwards(-x);
            append('-');
        }
        reverse();
    }

    /* assignment fron unsigned-int64. */
    op=(uint64 x) {
        fillDigitsBackwards(x);
        reverse();
    }

    /* move operator. */
    op<-(String^ s) {
        /* don't steal our own resources. */
        if (s == ^self) {
            return;
        }
        /* steal their resources. */
        delete storage_;
        size_ = s^.size_;
        capacity_ = s^.capacity_;
        storage_ <- s^.storage_;
        /* leave them in a valid state. */
        s^.size_ = 0;
        s^.capacity_ = 0;
    }

    /* concatenate two String's. */
    op+(String^ sa, String^ sb) {
        reserve(sa^.size_ + sb^.size_);
        self = sa^;
        append(sb^.storage_, sb^.size_);
    }

    /* set to empty string. */
    void clear() {
        size_ = 0;
    }

    /* increase capacity. save existing string. */
    void reserve(intptr cap) {
        if (cap <= capacity_) {
            return;
        }

        /* round up capacity to 16 byte chunks. */
        cap = (cap + 0x0F) & ~0x0F;

        new_storage = new char[cap];
        strcpy(new_storage, storage_, size_);

        capacity_ = cap;
        storage_ <- new_storage;
    }

    /* reverse the order of the characters. */
    void reverse() {
        lo = storage_;
        hi = storage_ + size_ - 1;
        while (lo < hi) {
            lo++^ <-> hi--^;
        }
    }

    /* print the string. */
    void print() {
        __print(storage_[0..size_]);
    }

    /** print the string on a line. */
    void println() {
        __println(storage_[0..size_]);
    }

/* private functions. */

    /* set to an array of characters. */
    void set(char[] arr, intptr sz) {
        reserve(sz);
        size_ = sz;
        strcpy(storage_, arr, sz);
    }

    /* append an array of characters. */
    void append(char[] arr, intptr sz) {
        expand(size_ + sz);
        strcpy(storage_ + size_, arr, sz);
        size_ += sz;
    }

    /* append a single character */
    void append(char c) {
        append(^c, 1);
    }

    /*
    expect the string to continue growing.
    add more capacity than requested.
    */
    void expand(intptr cap) {
        if (cap > capacity_) {
            cap += cap / 2;
        }
        reserve(cap);
    }

    /* convert x to a string of digits in reverse order. */
    void fillDigitsBackwards(uint64 x) {
        clear();
        reserve(24);
        while {
            append('0' + x % 10);
            x /= 10;
        } (x);
    }
}

/* helper functions. */

/* return length of null terminated string. */
intptr strlen(char[] s) {
    intptr len = 0;
    while () {
        char ch = s^;
        if (ch == 0) {
            break;
        }
        ++s;
        ++len;
    }
    return len;
}

/* copy null terminated string from src to dst. */
void strcpy(
    char[] dst,
    char[] src
) {
    while () {
        ch = src++^;
        dst++^ = ch;
        if (ch == 0) {
            break;
        }
    }
}

/* copy count characters from src to dst. */
void strcpy(
    char[] dst,
    char[] src,
    intptr count
) {
    while (count > 0) {
        ch = src++^;
        dst++^ = ch;
        if (ch == 0) {
            break;
        }
        --count;
    }
}
