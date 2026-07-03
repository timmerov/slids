/*
explore sort library.
*/

Vector(
    float32 array_[100]
) {
    float32^ const op[](int idx) {
        return ^array_[idx];
    }

    int const size() {
        return 100;
    }

    bool const less(float32 a, float32 b) {
        if (a < 0.0) {
            a = - a;
        }
        if (b < 0.0) {
            b = -b;
        }
        return (a < b);
    }
}

void sort(mutable Vector^ vec) {
    sort(vec, 0, vec^.size());
}

void sort(mutable Vector^ vec, int begin, int end) {
    while() {
        bool repeat = false;
        for (i : begin..end) {
            for (k : i+1..end) {
                i_less_k = vec^.less(vec^[i], vec^[k]);
                if (i_less_k == false) {
                    repeat = true;
                    vec^[i] <--> vec^[k];
                }
            }
        }
        if (repeat == false) {
            break;
        }
    }
}

int32 main() {

    x = 2.0;
    __println("type(x)=" + ##type(x));
    y = 2.0 * 2;
    __println("type(y)=" + ##type(y));
    a = 3;
    __println("type(a)=" + ##type(a));

    Vector vec;
    for (i : 0..vec.size()) {
        //vec[i] = 2.0 * i;
    }
    sort(^vec);

    return 0;
}
