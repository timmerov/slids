/*
pointers to functions.
*/

Container(int data_[5]) {
    _() {
        for (i : 0..5) {
            data_[i] = 2 - i;
        }
    }
    ~() {
    }

    int^ op[](int index) {
        return ^data_[index];
    }

    int const size() {
        return 5;
    }

    void print() {
        __print("[");
        for (i : 0..5) {
            __print(" " + data_[i]);
        }
        __println(" ]");
    }
}

/* alias the function type. */
alias LessFn = bool(int, int);

/* global less than. */
bool int_less_than(int a, int b) {
    return a < b;
}

void sort0(mutable Container^ container) {
    /* pointer to function passed as parameter. */
    sort1(container, ^int_less_than);
}

/* pointer to function declared as parameter. */
void sort1(mutable Container^ container, LessFn^ compare) {
    int size = container^.size();
    while() {
        bool repeat = false;
        for (i : 0..size) {
            for (k : i+1..size) {
                /* call pointed-to function. */
                /*
                well fark.
                this syntax sucks.
                but compare^() doesn't parse.
                defer until bigger fish get fried.
                */
                lt = (compare^)(container^[k], container^[i]);
                if (lt) {
                    repeat = true;
                    container^[k] <--> container^[i];
                }
            }
        }
        if (repeat == false) {
            break;
        }
    }
}

/* define a backwards less than in a namespace. */
Backwards {
    bool int_less_than(int a, int b) {
        return a > b;
    }
}

int32 main() {

    Container container;
    __print("init : ");
    container.print();

    sort0(^container);
    __print("sort0: ");
    container.print();

    sort1(^container, ^Backwards:int_less_than);
    __print("bkwds: ");
    container.print();

    bool local_less_than(int a, int b) {
        return a < b;
    }
    sort1(^container, ^local_less_than);
    __print("local: ");
    container.print();

    /*
    LocalSpace {
        bool abs_less(int a, int b) {
            if (a < 0) {
                a = - a;
            }
            if (b < 0) {
                b = - b;
            }
            return a < b;
        }
    }
    sort1(^container, ^LocalSpace:abs_less);
    __print("abslt: ");
    container.print();
    */

    return 0;
}
