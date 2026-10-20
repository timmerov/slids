import string;

(bool, int) divide(int a, int b) {
    if (b == 0) {
        return (false, 0);
    }
    return (true, a / b);
}

int32 main() {
    pair = (42, 'x');               // (int, char)
    int a = pair[0];                // constant index
    (ok, q) = divide(10, 3);        // destructure, types inferred
    (, r) = divide(9, 3);           // skip a slot

    sum = (1, 2, 3) + (10, 20, 30); // slot-wise: (11, 22, 33)
    scaled = sum * 2;               // broadcast: (22, 44, 66)

    int grid[2][3] = ((1, 2, 3), (4, 5, 6));
    int arr[3] = (7, 8, 9);
    (int, int) tpl = (arr[0], grid[1][2]);

    println(String + a + " " + pair[1] + " " + ok + " " + q + " " + r + " " + scaled[2] + " " + tpl[1]);
    return 0;
}
