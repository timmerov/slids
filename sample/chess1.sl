
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
        (kRook,   kBishop, kKnight, kQueen, kKing, kBishop, kKnight, kRook),
        (kPawn,   kPawn,   kPawn,   kPawn,  kPawn, kPawn,   kPawn,   kPawn),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kKnight, kEmpty),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kEmpty,  kEmpty,  kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kEmpty,  kKnight, kEmpty,  kEmpty, kEmpty, kEmpty,  kEmpty,  kEmpty),
        (kPawn,   kPawn,   kPawn,   kPawn,  kPawn,  kPawn,   kPawn,  kPawn),
        (kRook,   kEmpty,  kBishop, kQueen, kKing,  kBishop, kKnight, kRook)
    );

    int row = 0;
    int col = 0;
    for row in (0..8) {
        for col in (0..8) {
            if (board[row][col] == kKnight) {
                break rows;
            }
        } :cols
    } :rows
    __println("Knight found at row " + row + ", col " + col);

    return 0;
}
