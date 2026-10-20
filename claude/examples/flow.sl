import string;

enum Dir ( kNorth, kEast, kSouth, kWest );

int32 main() {
    int total = 0;
    for (i : 0..10) {                       // 0 to 9
        total += i;
    }
    for (i : 10..>0 - 2) {                  // 10, 8, 6, 4, 2
        total += i;
    }
    for (x : (1, 2, 3)) {                   // tuple
        total += x;
    }
    for (ch : "abc") {                      // string literal
        total += (int=ch);
    }
    for (d : Dir) {                         // enum, first to last
        total += d;
    }
    for (int j = 0, k = 10) (j < k) { ++j; --k; } {
        total += 1;
    }

    int n = 0;
    while (n < 3) {
        ++n;
    }
    while {
        --n;
    } (n > 0);
    while () {
        break;
    }

    for (i : 0..5) {
        for (j : 0..5) {
            if (j == 1) {
                continue;
            } else if (i == 3) {
                break outer;
            }
            total += 1;
        }
    } :outer;

    switch (total) {
        0: {
            println(String + "zero");
        }
        1: 2: {
            println(String + "small");
        } continue;                         // fall through
        default: {
            println(String + total);
        }
    }
    return 0;
}
