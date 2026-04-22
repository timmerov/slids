/*
test features of the move operator.
*/

NoMove(
    int x_
) {
}

Move(
    int x_
) {
    op<-(Move^ rhs) {
        x_ = rhs^.x_;
        __println("Move:<-=" + x_);
    }
}

/*
bug: this should compile, but doesn't.
unknown type for a_.
*/
NestedMove(
    Move a_,
    Move b_,
    Move c_
) {
    void print(char[] name) {
        __println("NestedMove:" + name + ": a=" + a_.x_ + " b=" + b_.x_ + " c=" + c_.x_);
    }
}

int32 main() {
    /* move pointers */
    char[] p1 = new char[100];
    char[] p2 <- p1;
    if (p1 == nullptr) {
        __println("p1 == nullptr");
    } else {
        __println("p1 != nullptr");
    }
    if (p2 == nullptr) {
        __println("p2 == nullptr");
    } else {
        __println("p2 != nullptr");
    }
    delete p2;
    delete p1;
    if (p1 == nullptr) {
        __println("p1 == nullptr");
    } else {
        __println("p1 != nullptr");
    }
    if (p2 == nullptr) {
        __println("p2 == nullptr");
    } else {
        __println("p2 != nullptr");
    }

    /* move with no move operator. */
    {
        NoMove from(42);
        NoMove to;
        __println("NoMove before: from=" + from.x_ + " to=" + to.x_);
        to <- from;
        __println("NoMove after : from=" + from.x_ + " to=" + to.x_);
    }

    /* move with move operator. */
    {
        Move from(42);
        Move to;
        __println("Move before: from=" + from.x_ + " to=" + to.x_);
        to <- from;
        __println("Move after : from=" + from.x_ + " to=" + to.x_);
    }

    /* move with nested move operator. */
    {
        NestedMove from(42, 37, 2026);
        NestedMove to;
        from.print("before from");
        to.print("before to  ");
        to <- from;
        from.print("after  from");
        to.print("after  to  ");
    }

    return 0;
}
