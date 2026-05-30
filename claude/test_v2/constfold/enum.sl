/*
test enums.

enums may be defined in any scope where constants may be defined.

the default type for enum is int.
values outside the range of int require an explicit type.

the fully qualified name must be used.

todo: land aliases.
the enum qualifier may be removed with alias.
*/

/*
enum /*int*/ Direction (
    kNorth,
    kSouth,
    kEast,
    kWest
);

void foo() {
    enum Language (
        kSlids,
        kCPP,
        kRust
    );

    Language lang = Language:kCPP;
    __println("lang = " + lang);

    /*
    reach goal: drop the qualifier in context.
    the type of the assigned variable is Language.
    the statement could get an implied alias Language.
    which ends at the semicolon.
    */
    Language best = kRust;
    best = kSlids;
}

/* todo after alias lands. */
/*
enum Bonk1 ( kOops );
enum Bonk2 ( kOops );
alias Bonk1;
alias Bonk2;
void bar() {
    /* compile error: ambiguous */
    int err = kOops;

    int good = Bonk1:kOops;
    int fine = Bonk2:kOops;
}
*/
*/

int32 main() {
/*
    enum float Consts (
        kPi = 3.14,
        kE = 2.718
    );

    __println("kNorth = " + Direction:kNorth);
    __println("kWest = " + Direction:kWest);
    __println("kPi = " + Consts:kPi);
    __println("kE = " + Consts:kE);

    /* todo after alias lands. */
    /*
    alias Direction;
    __println("kNorth = " + kNorth);
    __println("kPi = " + kPi);
    */
*/
    return 0;
}
