import string;
import vector;

T biggest<T>(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

Pair<K, V>(K key_, V value_) {
    V const get() {
        return value_;
    }
}

alias Integers = <int|int8|int16|int32|int64>;

(const char)[] kind<T=Integers>(T arg) {            // specialization by type set
    return "integer";
}
(const char)[] kind<T=[N]>(T arg) {                 // any fixed-size array
    return "array";
}
(const char)[] kind<T>(T arg) {                     // catch-all
    return "other";
}

int32 main() {
    m = biggest(3, 9);                      // T = int inferred
    f = biggest<float64>(1.5, 2.5);
    Pair<char, int> p('a', 42);

    Vector<int> v;
    v.append(1);
    v.append(2);
    int sum = 0;
    for (x : v) {
        sum += x;
    }
    int arr[3] = (1, 2, 3);
    println(String + m + " " + f + " " + p.get() + " " + sum + " " + kind(7) + " " + kind(arr) + " " + kind(2.5));
    return 0;
}
