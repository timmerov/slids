
enum Piece (
    kEmpty,
    kKing,
    kQueen,
    kRook,
    kBishop,
    kKnight,
    kPawn
)

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

    return 0;
}
