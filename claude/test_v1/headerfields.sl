/*
regression: a class split across a header and a source reopen.

Box's fields are declared in boxhdr.slh; the reopen here only adds
method bodies. The parser must see the header-declared fields so a
bare `field = expr` write parses as an assignment, not as an
inferred-type local declaration. The enum-typed field must also be
usable as the type of a local — inferred or explicit.
*/

import boxhdr;

Box() {
    _() {
    }

    ~() {
    }

    /* a class-body alias and const survive the header reopen. */
    alias Tick = int;
    const Tick kBonus = 10;

    void start() {
        /* bare writes to header-declared fields. */
        mode_ = Box:kRunning;
        ticks_ = 0;
    }

    void tick() {
        ticks_ = ticks_ + 1;
    }

    int report() {
        /* inferred local of the nested-enum field type. */
        m = mode_;
        /* explicit local of the nested-enum type. */
        Mode m2 = mode_;
        if (m == Box:kRunning && m2 == Box:kRunning) {
            Tick t = ticks_;
            return t + kBonus;
        }
        return -1;
    }
}

int32 main() {
    Box b;
    b.start();
    b.tick();
    b.tick();
    b.tick();
    __println("ticks=" + b.report());
    return 0;
}
