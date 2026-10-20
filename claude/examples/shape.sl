/* the implementation. */

import shape;

Shape(int scale_ = 1) {                     // no trailing ... : completes the class
    _() { }
    ~() { }
    int area() {
        return w_ * h_ * scale_;
    }
}

int twice(int x) {
    return 2 * x;
}

int helper() {                              // not in the header: private
    return 0;
}
