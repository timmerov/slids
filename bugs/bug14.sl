/*
compile fails.
*/

int32 main() {
    __println("Hello, World!");

    int arr[4] = (1, 2, 3, 4);

    /* prints int */
    __println(##type(arr[0]));

    /*
    this is now correctly a compile error.
    you cannot assign an int to an int^.
    */
    //int[] p1 = arr[0];
    //int[] p2 = arr[3];

    int[] p1 = ^arr[0];
    int[] p2 = ^arr[3];

    /* parentheses on rhs. */
    p1^ = (p2--)^;
    __println("arr[4, 2, 3, 4]=(" + arr[0] + "," + arr[1] + "," + arr[2] + "," + arr[3] + ")");

    /*
    reset the array.
    this was a compile error.
    weird that after 30+ days i never tried to directly.
    set the value of an array.
    always went through a pointer.
    */
    arr[0] = 1;

    /* check pointers. */
    bool eq1 = (p2 == ^arr[3]);
    bool eq2 = (p2 == ^arr[2]);
    __println("eq1[0]=" + eq1);
    __println("eq2[1]=" + eq2);

    /* this should compile but doesn't */
    //(p1++)^ = (p2--)^;

    __println("Goodbye, World!");
    return 0;
}
