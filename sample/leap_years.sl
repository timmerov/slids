import std.io;

int32 main() {
    for int year in (1992..2109) {
        bool by4   = year % 4   == 0;
        bool by100 = year % 100 == 0;
        bool by400 = year % 400 == 0;

        if (by400 || (by4 && !by100)) {
            println(year);
        }
    }
    return 0;
}
