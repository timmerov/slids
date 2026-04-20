
Vector3(
    int x_,
    int y_,
    int z
) {
}

int32 main() {
    int x;
    Vector3 vec;
    int arr[5];

    __println("sizeof(char) = 1 = " + sizeof(char));
    __println("sizeof(int) = 4 = " + sizeof(int));
    __println("sizeof(intptr) = 8 = " + sizeof(intptr));
    __println("sizeof(int8) = 1 = " + sizeof(int8));
    __println("sizeof(int16) = 2 = " + sizeof(int16));
    __println("sizeof(int32) = 4 = " + sizeof(int32));
    __println("sizeof(int64) = 8 = " + sizeof(int64));
    __println("sizeof(void^) = 8 = " + sizeof(void^));
    __println("sizeof(Vector3) = 12 = " + sizeof(Vector3));
    __println("sizeof(x) = 4 = " + sizeof(x));
    __println("sizeof(^x) = 8 = " + sizeof(^x));
    __println("sizeof(vec) = 12 = " + sizeof(vec));
    __println("sizeof(arr) = 20 = " + sizeof(arr));

    return 0;
}
