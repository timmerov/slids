
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
    Piece board[8][8] = (
        (kRook,   kBishop, kKnight, kQueen, kKing, kBishop, kEmpty, kRook),
        (kPawn,   kPawn,   kPawn,   kPawn,  kPawn, kPawn,   kPawn,   kPawn),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kKnight, kEmpty),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kEmpty,  kKnight, kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kPawn,   kPawn,   kPawn,   kPawn,  kPawn,  kPawn,   kPawn,  kPawn),
        (kRook,   kEmpty,  kBishop, kQueen, kKing,  kBishop, kKnight, kRook)
    );


    int counter = 0;
    for (int row : 0..8) {
        Piece[] ptr = ^board[row][0];
        for (int col : 0..8) {
            if (ptr^ == kKnight) {
                ++counter;
                __println(counter + ": Knight found at row " + row + ", col " + col);
            }
            ++ptr;
        }
    }

    return 0;
}
