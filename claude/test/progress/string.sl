/*
String class implementation.

also included are helper functions for:
concatenating strings,
length of a null terminated string,
copying null terminated strings,
copying null terminated strings limited by length.

work in progress.
search for tsc.
*/

import string;

/* import c libaries. */
/* ==tsc==
stdc import {
    /* converts float to c string. */
    int32 strfromd(mutable char[] s, intptr n, char[] fmt, float64 fp);
}
*/

/* block definitions. */
String (
    /* length of stored string. */
    intptr size_ = 0,

    /* maximum size storable. */
    intptr capacity_ = 0,

    /* pointer to stored string. */
    char[] storage_ = nullptr
) {
    /* maximum size for converting numbers. */
    const intptr kNumberBufferSize = 32;

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
    op<--(mutable String^ s) {
        /* don't steal our own resources. */
        if (s == ^self) {
            return;
        }
        /* steal their resources. */
        delete storage_;
        size_ = s^.size_;
        capacity_ = s^.capacity_;
        storage_ <-- s^.storage_;
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
        set(<char[]> ^c, 1);
    }

    /*
    assignment and type conversion from int64.
    all smaller signed ints should promote to int64.
    excluding char.
    */
    op=(int64 x) {
        ux = (uint64=x);
        if (x >= 0) {
            fillDigitsBackwards(ux);
        } else {
            fillDigitsBackwards(-ux);
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

    /* assignment and type converstion from float64. */
    op=(float64 x) {
        clear();
        reserve(kNumberBufferSize);
        // ==tsc==
        //size_ = stdc:strfromd(storage_, capacity_, "%g", x);
        size_ = 0;
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
        append(<char[]> ^c, 1);
    }


    /* overload += to append a bool. */
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
            stmp = self;
            self = sa^ + stmp;
        } else {
            /* this works even if sa is self. */
            reserve(sa^.size_ + sb^.size_);
            self = sa^;
            append(sb^.storage_, sb^.size_);
        }
    }

    /* binary equals */
    bool const op==(String^ s) {
        cmp = compare(s);
        return (cmp == 0);
    }

    /* binary not equals */
    bool const op!=(String^ s) {
        cmp = compare(s);
        return (cmp != 0);
    }

    /* binary less than or equals */
    bool const op<=(String^ s) {
        cmp = compare(s);
        return (cmp <= 0);
    }

    /* binary greater than or equals. */
    bool const op>=(String^ s) {
        cmp = compare(s);
        return (cmp >= 0);
    }

    /* binary less than. */
    bool const op<(String^ s) {
        cmp = compare(s);
        return (cmp < 0);
    }

    /* binary greater than */
    bool const op>(String^ s) {
        cmp = compare(s);
        return (cmp > 0);
    }

    /*
    overload index operation to fetch/set the indexed character.
    char ch = str[3];
    str[3] = '!';
    */
    char^ const op[](intptr index) {
        return ^storage_[index];
    }

    /* return size of string. */
    intptr const size() {
        return size_;
    }

    /* begin iterator */
    char^ const begin() {
        return storage_;
    }

    /* end iterator */
    char^ const end() {
        return storage_ + size_;
    }

    /* update iterator */
    char^ const next(char^ prev) {
        /*
        reinterpret the refernce to const:
            (const char)^ prev
        as a mutable iterator.
            char[] iter
        */
        iter = <char[]> <mutable> prev;
        return iter + 1;
    }

    /* set to empty string. */
    void clear() {
        size_ = 0;
    }

    /* return true if the string is empty. */
    bool const empty() {
        return (size_ == 0);
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
        storage_ <-- new_storage;
    }

    /* reverse the order of the characters. */
    void reverse() {
        for (
            lo = storage_,
            hi = storage_ + size_ - 1
        ) (lo < hi) {
            ++lo; --hi;
        } {
            lo^ <--> hi^;
        }
    }

    /*
    compare this string to another. results:
        -1: self <  other
         0: self == other
        +1: self >  other
    */
    int const compare(String^ other) {
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
        /* unreachable but required by the compiler. */
        return 0;
    }

    /* return the substring. */
    String const slice(intptr start, intptr count) {
        String s;
        s.reserve(count);
        s.append(storage_ + start, count);
        return s;
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
        append(<char[]> ^c, 1);
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
            digit = '0' + (char=x % 10);
            append(digit);
            x /= 10;
        } (x);
    }
}

/* print the string. */
void print(String^ s) {
    // ==tsc==
    //__print(s^.storage_[0..s^.size_]);
}

/** print the string on a line. */
void println(String^ s) {
    // ==tsc==
    //__println(s^.storage_[0..s^.size_]);
}

/* helper functions. */

/* return length of null terminated string. */
intptr strlen(char[] s) {
    intptr len = 0;
    for () (s^) { ++s; ++len; } {}
    return len;
}

/* copy null terminated string from src to dst. */
void strcpy(
    mutable char[] dst,
    char[] src
) {
    for (ch = 'A') (ch) { ++src; ++dst; } {
        ch = src^;
        dst^ = ch;
    }
}

/* copy count characters from src to dst. */
void strcpy(
    mutable char[] dst,
    char[] src,
    intptr count
) {
    for (ch = 'A') (count > 0 && ch) {
        --count; ++src; ++dst;
    } {
        ch = src^;
        dst^ = ch;
    }
}

/* fill the string with the character. */
void strfill(
    mutable char[] s,
    char ch,
    intptr count
) {
    for() (count > 0) {
        ++s; --count;
    } {
        s^ = ch;
    }
}

/* formatted strings. */
/* ==tsc==
String : Format() {
    /* constructor/destructor */
    _() {
    }
    ~() {
    }

    /*
    format a string.
    rhs may be self.
    */
    op=(String^ rhs) {
        if (rhs == ^self) {
            String temp = rhs^;
            self = temp;
            return;
        }

        /* reserve space for the justified width. */
        intptr rhs_size = rhs^.size_;
        intptr width = min_width_;
        if (width < rhs_size) {
            width = rhs_size;
        }
        if (max_width_ >= 0 && width > max_width_) {
            width = max_width_;
        }
        if (rhs_size > width) {
            rhs_size = width;
        }
        reserve(width);
        size_ = width;

        /* number of pad characters. */
        fill_count = width - rhs_size;

        /* right or left justify the string. */
        if (justify_ == Format:kRightJustify) {
            strfill(storage_, pad_, fill_count);
            strcpy(storage_ + fill_count, rhs^.storage_, rhs_size);
        } else {
            strcpy(storage_, rhs^.storage_, rhs_size);
            strfill(storage_ + rhs_size, pad_, fill_count);
        }
    }

    /* format a null terminated string literal. */
    op=(char[] s) {
        String str = s;
        self = str;
    }

    /* format a character literal. */
    op=(char c) {
        String str = c;
        self = str;
    }

    /*
    format an int64.
    all smaller signed ints should promote to int64.
    excluding char.
    */
    op=(int64 x) {
        String str;
        str.reserve(kNumberBufferSize);
        if (x >= 0 && leading_plus_) {
            str = "+";
        }
        str += x;
        self = str;
    }

    /*
    format a uint64.
    all smaller unsigned ints should promote to uint64.
    excluding char.
    */
    op=(uint64 x) {
        String str = x;
        self = str;
    }

    /* format a bool. */
    op=(bool b) {
        String str = b;
        self = str;
    }

    /* format a float64. */
    op=(float64 x) {
        String str;
        str.reserve(kNumberBufferSize);
        if (x >= 0.0 && leading_plus_) {
            str = "+";
        }
        char style;
        switch (float_style_) {
        case Format:kFixedPoint:
            style = 'f';
            break;
        case Format:kScientificNotation:
            style = 'e';
            break;
        default:
            style = 'g';
            break;
        }
        /* build format c-string. */
        fmt = String + "%." + precision_ + style + '\0';
        // ==tsc==
        //str.size_ += stdc:strfromd(str.storage_ + str.size_, str.capacity_ - str.size_, fmt.storage_, x);
        self = str;
    }

    /* overload * to format a string in place. */
    op*(Format^ fmt, String^ s) {
        copyFormatClear(fmt);
        self = s^;
    }

    /* overload * to format a signed number in place. */
    op*(Format^ fmt, int64 x) {
        copyFormatClear(fmt);
        self = x;
    }

    /* overload * to format an unsigned number in place. */
    op*(Format^ fmt, uint64 x) {
        copyFormatClear(fmt);
        self = x;
    }

    /* overload * to format a floating point number in place. */
    op*(Format^ fmt, float64 x) {
        copyFormatClear(fmt);
        self = x;
    }

/* private methods */

    /* copy the format fields. */
    void copyFormatClear(Format^ fmt) {
        clear();
        x = fmt^.justify_;
        justify_      = fmt^.justify_;
        float_style_  = fmt^.float_style_;
        pad_          = fmt^.pad_;
        leading_plus_ = fmt^.leading_plus_;
        min_width_    = fmt^.min_width_;
        max_width_    = fmt^.max_width_;
        precision_    = fmt^.precision_;
    }
}
*/
