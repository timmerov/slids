
Simple() {
    op=(int x) {
        __println("op=(int)");
    }
}

int32 main() {

    Simple a;
    /*
    this calls op=(int).
    it should call op=(Simple^).
    if that is not explicitly defined...
    then the compiler should create one.
    the default is to copy field by field.
    */
    Simple b = a;

    return 0;
}
