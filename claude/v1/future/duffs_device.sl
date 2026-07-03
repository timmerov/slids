import std.io;

int32 main() {
    int count = 20;
    int[] src = getSomeData();
    int[] dst = getSomeDest();

    int n = (count + 7) / 8;

    switch (count % 8) {
    case 0:
        while (n > 0) {
            dst++^ = src++^;
    case 7:
            dst++^ = src++^;
    case 6:
            dst++^ = src++^;
    case 5:
            dst++^ = src++^;
    case 4:
            dst++^ = src++^;
    case 3:
            dst++^ = src++^;
    case 2:
            dst++^ = src++^;
    case 1:
            dst++^ = src++^;
            n--;
        }
    }

    return 0;
}
