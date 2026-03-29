import std.io;

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

    void namedBreakMethod() {
        int row = 0;
        int col = 0;

        for row in (0..8) :rows {
            for col in (0..8) :cols {
                if (board[row][col] == kKnight) {
                    break rows;
                }
            }
        }

        println("Knight found at row " + row + ", col " + col);
    }

    (int row, int col) returnTupleMethod() {
        for int row in (0..8) {
            for int col in (0..8) {
                if (board[row][col] == kKnight) {
                    return (row, col);
                }
            }
        }
        return (-1, -1);
    }

    namedBreakMethod();

    (int row, int col) = returnTupleMethod();
    println("Knight found at row " + row + ", col " + col);

    return 0;
}
