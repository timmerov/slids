/*
String class implementation.

also included are hellper functions for:
concatenating strings,
length of a null terminated string,
copying null terminated strings,
copying null terminated strings limited by length.
*/

import string;

/* block definitions. */
String {

    /* constructor */
    _() {
    }

    /* destructor */
    ~() {
        delete storage_;
    }

    /* assignment from String. */
    void op=(String^ s) {
        set(s^.storage_, s^.size_);
    }

    /* assignment from null terminated string. */
    void op=(char[] str) {
        int len = strlen(str);
        set(str, len);
    }

    /* assignment from char. */
    void op=(char c) {
        set(^c, 1);
    }

    /* assignment fron int64. */
    void op=(int64 x) {
        if (x >= 0) {
            fillDigitsBackwards(x);
        } else {
            fillDigitsBackwards(-x);
            append('-');
        }
        reverse();
    }

    /* assignment fron unsigned-int64. */
    void op=(uint64 x) {
        fillDigitsBackwards(x);
        reverse();
    }

    /* move operator. */
    void op<-(String^ s) {
        delete storage_;
        size_ = s^.size_;
        capacity_ = s^.capacity_;
        storage_ = s^.storage_;
        s^.size_ = 0;
        s^.capacity_ = 0;
        s^.storage_ = nullptr;
    }

    /* set to empty string. */
    void clear() {
        size_ = 0;
    }

    /* increase capacity. save existing string. */
    void reserve(int cap) {
        if (cap <= capacity_) {
            return;
        }

        char[] new_storage = new char[cap];
        strcpy(new_storage, storage_, size_);

        capacity_ = cap;
        storage_ <- new_storage;
    }

    /* reverse the order of the characters. */
    void reverse() {
        char[] lo = storage_;
        char[] hi = storage_ + size_ - 1;
        while (lo < hi) {
            char c = lo^;
            lo^ = hi^;
            hi^ = c;
            ++lo;
            --hi;
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
    void set(char[] arr, int sz) {
        reserve(sz);
        size_ = sz;
        strcpy(storage_, arr, sz);
    }

    /* append an array of characters. */
    void append(char[] arr, int sz) {
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
    void expand(int cap) {
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

/* concatenate two String's. */
String op+(String^ sa, String^ sb) {
    String sc;
    sc.reserve(sa^.size_ + sb^.size_);
    sc = sa^;
    sc.append(sb^.storage_, sb^.size_);
    return sc;
}

/* helper functions. */

/* return length of null terminated string. */
int strlen(char[] s) {
    int len = 0;
    while (true) {
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
    while (true) {
        char ch = dst++^;
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
    int count
) {
    while (count > 0) {
        char ch = src++^;
        dst++^ = ch;
        if (ch == 0) {
            break;
        }
        --count;
    }
}
