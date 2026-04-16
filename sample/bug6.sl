
Simple() {
}

Simple op+(Simple^ sa, Simple^ sb) {
    __println("op+");
    Simple sc;
    return sc;
}

int32 main() {
    Simple sa;
    Simple sb;
    __println("expected: op+");

    /*
    this should call Simple:op+
    but it doesn't.
    */
    Simple sc = sa + sb;

    return 0;
}
