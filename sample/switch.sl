
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

    println("expected: None King Queen Rook Bishop Knight Pawn");
    for Piece piece in Piece {
        switch (piece) {
        case kKing:
            print("King");
            break;

        case kQueen:
            print("Queen");
            break;

        case kRook:
            print("Rook");
            break;

        case kBishop:
            print("Bishop");
            break;

        case kKnight:
            print("Knight");
            break;

        case kPawn:
            print("Pawn");
            break;

        default:
            print("None");
            break;
        }
        print(" ");
    }
    println();

    return 0;
}
