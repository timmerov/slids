/*
resolve the bug of the day.
*/

/* should not be compile error. */
enum EG ( eG1, eG2 = eG1 );

int32 main() {

    enum EF ( eF1, eF2 = eF1 );

    return 0;
}
