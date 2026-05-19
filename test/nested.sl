/*
nested classes.
*/

Outer(
    int x_ = 1,
    int y_ = 2,
    ...
) {
    Inner(
        int a_ = 3,
        int b_ = 4,
        Tag tag_ = kLow
    ) {
        /* a const and a nested enum inside a nested class. */
        const int kBase = 30;
        enum Tag (kLow, kHigh);

        void print(char[] name) {
            __println("Inner: " + name + ": a=" + a_ + " b=" + b_);
        }

        /* inferred local of the nested-enum field type. */
        int tag() {
            t = tag_;
            return t;
        }
    }

    void test1() {
        Inner inn(5);
        inn.print("Outer:test inn");
    }

    InTemplate<T>(
        T m_
    ) {
        void print(char[] name) {
            __println("InTemplate: " + ##type(m_) + " " + name + " = " + m_);
        }
    }

    void test2() {
        InTemplate<uint16> it(10);
        __println("Outer:test2 it");
    }
}

Outer (
    ...,
    int z_ = 100
) {
    Inner2(
        int c_ = 101
    ) {
        void print(char[] name) {
            __println("Inner2: " + name + ": c=" + c_);
        }
    }
}

int32 main() {
    Outer out;
    out.test1();
    out.test2();

    Outer:Inner main_in(6, 7, Outer:Inner:kHigh);
    main_in.print("main_inn");
    __println("main_inn tag = " + main_in.tag());

    Outer:InTemplate<char> main_it(11);
    main_it.print("main_it");

    Outer:Inner2 main_in2(102);
    main_in2.print("main_in2");

    /* nested-class const and nested-enum value via multi-colon scope. */
    __println("Outer:Inner:kBase = " + Outer:Inner:kBase);
    __println("Outer:Inner:kHigh = " + Outer:Inner:kHigh);

    return 0;
}
