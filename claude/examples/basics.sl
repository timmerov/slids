import string;

const int kSize = 4;
const int kTwice = 2 * kSize;

int32 main() {
    int8 small = 100;
    x = 42;                         // int
    big = 5_000_000_000;            // int64
    mask = 0xFF_00;                 // uint
    f = 3.14;                       // float (32-bit)
    ch = 'A';                       // char
    ok = true;                      // bool
    s = "text";                     // const char[5]

    int sum = small + x;            // int8 widens to int
    i = (int=f);                    // conversion: truncates to 3
    n int = 2.5 * kTwice;           // shorthand for n = (int=2.5 * kTwice)
    const y = x;                    // run-time const
    x += 1;

    if (mask && !(i == 0)) {
        println(String + sum + " " + big + " " + i + " " + n + " " + ch + " " + ok + " " + y + " " + s + " " + kTwice + " " + x);
    }
    return 0;
}
