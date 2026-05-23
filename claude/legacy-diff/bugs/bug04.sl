
Record(
    /*
    this should compile, but doesn't.
    */
    char name_[16]
) {
}

int32 main() {
    Record record;
    intptr size = sizeof(record);
    __println("sizeof(record) should be: 16 is: "+size);
    return 0;
}
