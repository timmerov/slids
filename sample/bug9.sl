
Simple(
    int x_ = 0
) {
    op=(Simple^ rhs) {
        x_ = rhs^.x_;
        __println("success! self assigned to other in copy.");
    }

    void copy(Simple^ other) {
        /* this typo should not compile. */
        //self = ^other;

        /* correct code. */
        self = other^;
    }
}

int32 main() {

    __println("expected: success!");
    Simple a;
    Simple b;
    a.copy(a);
    b.copy(a);

    return 0;
}
