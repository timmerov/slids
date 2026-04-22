/*
missing return
*/

Simple(
    int x_,
    int y_,
    int z_
) {
    Simple clone() {
        Simple s = self;
        /*
        clone has a non-void return type.
        but never returns anything.
        this should be a compiler error:
        missing return statement.
        */
    }
}

int32 main() {
    Simple s0(1, 2, 3);
    Simple s1 = s0.clone();
    __println("s1: x=" + s1.x_ + " y=" + s1.y_ + " z=" + s1.z_);

    return 0;
}
