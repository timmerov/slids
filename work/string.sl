/*
String class implementation.

also included are helper functions for:
concatenating strings,
length of a null terminated string,
copying null terminated strings,
copying null terminated strings limited by length.
*/

import string;

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

    /*
    assignment from String.
    copy operator.
    rhs may be self.
    */
    op=(String^ s) {
        /* don't copy ourself. */
        if (s == ^self) {
            return;
        }
        set(s^.storage_, s^.size_);
    }

    /*
    move operator.
    rhs may be self.
    */
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

    /*
    assignment and type conversion from
    null terminated string literal.
    */
    op=(char[] str) {
        len = strlen(str);
        set(str, len);
    }

    /*
    assignment and type conversion from
    character literal.
    */
    op=(char c) {
        set(^c, 1);
    }

    /*
    assignment and type conversion from int64.
    all smaller signed ints should promote to int64.
    excluding char.
    */
    op=(int64 x) {
        if (x >= 0) {
            fillDigitsBackwards(x);
        } else {
            fillDigitsBackwards(-x);
            append('-');
        }
        reverse();
    }

    /*
    assignment and type conversion from unsigned-int64.
    all smaller unsigned ints should promote to uint64.
    excluding char.
    */
    op=(uint64 x) {
        fillDigitsBackwards(x);
        reverse();
    }

    /* assignment and type converstion from bool. */
    op=(bool b) {
        if (b) {
            set("true", 4);
        } else {
            set("false", 5);
        }
    }

    /* overload += to append a string. */
    op+=(String^ s)  {
        /* we can append ourselves to ourselves. */
        if (s == ^self) {
            /*
            ensure there is enough capacity
            before appending ourselves.
            */
            expand(2 * size_);
        }
        append(s^.storage_, s^.size_);
    }

    /* overload += to append a string literal. */
    op+=(char[] s)  {
        len = strlen(s);
        append(s, len);
    }

    /* overload += to append a character. */
    op+=(char c) {
        append(^c, 1);
    }


    /* overload += to append a bool. *./
    op+=(bool b) {
        if (b) {
            append("true", 4);
        } else {
            append("false", 5);
        }
    }

    /* overload + to concatenate two String-s. */
    op+(String^ sa, String^ sb) {
        /* special case if sb is self. */
        if (sb == ^self) {
            sc = sb;
            self = sa + sc;
        } else {
            /* this works even if sa is self. */
            reserve(sa^.size_ + sb^.size_);
            self = sa^;
            append(sb^.storage_, sb^.size_);
        }
    }

    /* set to empty string. */
    void clear() {
        size_ = 0;
    }

    /*
    increase capacity.
    preserves existing value.
    */
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

    /*
    compare this string to another. results:
        -1: self <  other
         0: self == other
        +1: self >  other
    */
    int compare(String^ other) {
        if (other == ^self) {
            return 0;
        }
        sizea = size_;
        sizeb = other^.size_;
        ptra = storage_;
        ptrb = other^.storage_;
        while () {
            if (sizea == 0 && sizeb == 0) {
                return 0;
            }
            if (sizea == 0) {
                return -1;
            }
            if (sizeb == 0) {
                return +1;
            }
            ca = ptra++^;
            cb = ptrb++^;
            if (ca > cb) {
                return +1;
            }
            if (ca < cb) {
                return -1;
            }
            if (ca == 0) {
                return 0;
            }
            --sizea;
            --sizeb;
        }
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

/* binary equals */
bool op==(
    String^ sa,
    String^ sb
) {
    cmp = sa^.compare(sb);
    return (cmp == 0);
}

/* binary not equals */
bool op!=(
    String^ sa,
    String^ sb
) {
    cmp = sa^.compare(sb);
    return (cmp != 0);
}

/* binary less than or equals */
bool op<=(
    String^ sa,
    String^ sb
) {
    cmp = sa^.compare(sb);
    return (cmp <= 0);
}

/* binary greater than or equals. */
bool op>=(
    String^ sa,
    String^ sb
) {
    cmp = sa^.compare(sb);
    return (cmp >= 0);
}

/* binary less than. */
bool op<(
    String^ sa,
    String^ sb
) {
    cmp = sa^.compare(sb);
    return (cmp < 0);
}

/* binary greater than */
bool op>(
    String^ sa,
    String^ sb
) {
    cmp = sa^.compare(sb);
    return (cmp > 0);
}

/* print the string. */
void print(String^ s) {
    __print(s^.storage_[0..s^.size_]);
}

/** print the string on a line. */
void println(String^ s) {
    __println(s^.storage_[0..s^.size_]);
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
