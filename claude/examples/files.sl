import shape;

int32 puts((const char)[] s) = import;      // a C function

Posix import {                              // a block of them in a namespace
    int32 getpid();
}

int32 main() {
    Shape s(3, 4);
    puts("from C");
    if (Posix:getpid() > 0 && twice(s.area()) == 24) {
        puts("ok");
    }
    return 0;
}
