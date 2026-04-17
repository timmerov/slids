
SimpleUndefined() {
    op=(int x) {
        __println("op=(int)");
    }
}

SimpleDefined() {
    op=(int x) {
        __println("op=(int)");
    }

    op=(SimpleDefined^ other) {
        __println("op=(Simple)");
    }
}

int32 main() {

    SimpleUndefined a;
    /*
    this calls op=(int).
    it should call op=(Simple^).
    if that is not explicitly defined...
    then the compiler should create one.
    the default is to copy field by field.
    */
    __println("expected: <nothing>");
    SimpleUndefined b = a;

    SimpleDefined c;
    __println("expected: op=(Simple)");
    SimpleDefined d = c;

    return 0;
}
