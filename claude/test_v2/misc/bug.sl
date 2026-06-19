/*
the bug of the day.
*/

/*
claude is forbidden from modifying this file
and its golden file.
*/

int32 main() {

    (int[2], int[2]) tuple = ((1,2), (3,4));
    (int, int) array[2] = ((5,6), (7,8));
    array = tuple;
    __println(tuple[0][0]);
    __println(array[0][0]);

    return 0;
}
