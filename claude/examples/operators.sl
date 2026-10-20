import string;

Vec(int x_ = 0, int y_ = 0) {
    op=(int v) {                            // assign from int
        x_ = v;
        y_ = v;
    }
    op+(Vec^ a, Vec^ b) {                   // the product is self
        x_ = a^.x_ + b^.x_;
        y_ = a^.y_ + b^.y_;
    }
    op+=(Vec^ v) {
        x_ += v^.x_;
        y_ += v^.y_;
    }
    bool const op==(Vec^ v) {
        return x_ == v^.x_ && y_ == v^.y_;
    }
    int^ op[](int i) {
        if (i == 0) {
            return ^x_;
        }
        return ^y_;
    }
}

Buffer(char[] data_ = nullptr) {
    _() {
        data_ = new char[16];
    }
    ~() {
        delete data_;
    }
    op<--(mutable Buffer^ b) {              // move: steal, leave b valid
        delete data_;
        data_ <-- b^.data_;                 // b^.data_ is now nullptr
    }
}

int32 main() {
    Vec a(1, 2);
    Vec b = 10;                             // op=(int)
    Vec c = a + b;
    c += a;
    c[1] = 99;
    v = (Vec=5);                            // conversion through op=

    Buffer src;
    Buffer dst <-- src;
    x = 1;
    y = 2;
    x <--> y;                               // swap

    println(String + c[0] + " " + c[1] + " " + (a == a) + " " + v[0] + " " + (src.data_ == nullptr) + " " + x);
    return 0;
}
