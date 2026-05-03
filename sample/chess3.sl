
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
        (kRook,   kBishop, kEmpty,  kQueen, kKing,  kBishop, kKnight, kRook),
        (kPawn,   kPawn,   kPawn,   kPawn,  kPawn,  kPawn,   kPawn,   kPawn),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kKnight, kEmpty),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kEmpty,  kKnight, kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kPawn,   kPawn,   kPawn,   kPawn,  kPawn,  kPawn,   kPawn,   kPawn),
        (kRook,   kEmpty,  kBishop, kQueen, kKing,  kBishop, kKnight, kRook)
    );

    (int row, int col) returnTupleMethod() {
        for (int row : 0..8) {
            for (int col : 0..8) {
                if (board[row][col] == kKnight) {
                    return (row, col);
                }
            }
        }
        return (-1, -1);
    }

    (int row, int col) = returnTupleMethod();
    __println("Knight found at row " + row + ", col " + col);

    return 0;
}
