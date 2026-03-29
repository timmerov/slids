import std.io;

int32 main() {
    for int i in (1..100) {
        bool by3 = i % 3 == 0;
        bool by5 = i % 5 == 0;
        if (by3 && by5) {
            println("biz-buzz");
        } else if (by3) {
            println("biz");
        } else if (by5) {
            println("buzz");
        } else {
            println(i);
        }
    }
    return 0;
}
