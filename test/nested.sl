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
        int b_ = 4
    ) {
        void print(char[] name) {
            __println("Inner: " + name + ": a=" + a_ + " b=" + b_);
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

    Outer:Inner main_in(6,7);
    main_in.print("main_inn");

    Outer:InTemplate<char> main_it(11);
    main_it.print("main_it");

    Outer:Inner2 main_in2(102);
    main_in2.print("main_in2");

    return 0;
}
