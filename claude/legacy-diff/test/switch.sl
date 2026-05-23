
enum Piece (
    kEmpty,
    kKing,
    kQueen,
    kRook,
    kBishop,
    kKnight,
    kPawn
);

/* a class with a nested enum, switched on a field of that type. */
Light(Color hue_ = kRed) {
    enum Color (
        kRed,
        kYellow,
        kGreen
    );
    _() {
    }
    ~() {
    }
    (const char)[] name() {
        (const char)[] r;
        switch (hue_) {
        case Light:kRed:        /* qualified case label */
            r = "red";
            break;
        case kYellow:           /* bare label — resolved against the enum */
            r = "yellow";
            break;
        case Light:kGreen:
            r = "green";
            break;
        default:
            r = "?";
            break;
        }
        return r;
    }
}

int32 main() {

    for (Piece piece : Piece) {
        switch (piece) {
        case kKing:
            __print("King");
            break;

        case kQueen:
            __print("Queen");
            break;

        case kRook:
            __print("Rook");
            break;

        case kBishop:
            __print("Bishop");
            break;

        case kKnight:
            __print("Knight");
            break;

        case kPawn:
            __print("Pawn");
            break;

        default:
            __print("None");
            break;
        }
        __print(" ");
    }
    __println();

    /* switch over a nested-enum field: qualified and bare case labels. */
    Light a;
    Light b(Light:kYellow);
    Light c(Light:kGreen);
    __println(a.name());
    __println(b.name());
    __println(c.name());

    return 0;
}
