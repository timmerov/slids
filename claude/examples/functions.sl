import string;

Point(int x_ = 0, int y_ = 0) { }

int add(int a, int b = 1, c = 2) {          // defaults; c inferred as int
    return a + b + c;
}

int add(float64 a) {                        // overload
    return (int=a);
}

int norm(Point^ p) {                        // classes, tuples, arrays: by address
    return p^.x_ + p^.y_;
}

void fill(mutable int[] dst, int n) {       // mutable: may write the caller's data
    for (i : 0..n) {
        dst[i] = i;
    }
}

int32 main() {
    Point p(3, 4);
    int arr[3] = (0, 0, 0);
    fill(arr, 3);

    int factor = 10;
    int scale(int v) {                      // nested function sees the locals
        return v * factor;
    }

    println(String + add(1) + " " + add(2.9) + " " + norm(p) + " " + arr[2] + " " + scale(5));
    return 0;
}
