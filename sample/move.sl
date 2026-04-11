
int32 main() {
    char[] p1 = new char[100];
    char[] p2 <- p1;
    __println("expected: ==");
    if (p1 == nullptr) {
        __println("p1 == nullptr");
    } else {
        __println("p1 != nullptr");
    }
    __println("expected: !=");
    if (p2 == nullptr) {
        __println("p2 == nullptr");
    } else {
        __println("p2 != nullptr");
    }
    delete p2;
    delete p1;
    __println("expected: ==");
    if (p1 == nullptr) {
        __println("p1 == nullptr");
    } else {
        __println("p1 != nullptr");
    }
    __println("expected: ==");
    if (p2 == nullptr) {
        __println("p2 == nullptr");
    } else {
        __println("p2 != nullptr");
    }
    return 0;
}
