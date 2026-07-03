/*
develop syntax for ranged for.
*/

Loop(
    int low_,
    int high_
) {
    int begin() {
        return low_;
    }

    int end() {
        return high_ + 1;
    }
}

int32 main() {

    /* print the ints in the tuple. */
    for x in (1, 1, 2, 3, 5, 8) {
        __println("tuple: x=" + x);
    }

    /* print one character per line. */
    for ch in "Hello" {
        __println("string literal: ch=" + ch);
    }

    /* print the numbers 17 to 21. */
    Loop lp(17, 21);
    for a in lp {
        __println("Loop: a=" + a);
    }

    return 0;
}
