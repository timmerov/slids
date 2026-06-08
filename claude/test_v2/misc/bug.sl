/*
the bug of the day.
*/

int32 main() {

    start = 1;
    end = 3;
    for (x : start..end) {
        __println(x);
    }
/*
    int array[2,3] = ((1,2), (3,4), (5,6));
    __println(##type(array[0]));

    /*for (row : array) {
        for (x : row) {
            __print(x + " ");
        }
        __println();
    }*/

    tuple = ((1,2), (3,4), (5,6));
    for (sub : tuple) {
        for (x : sub) {
            __print(x + " ");
        }
        __println();
    }
*/
    return 0;
}
