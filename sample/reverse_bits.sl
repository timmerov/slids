import std.io;

int32 main() {
    uint8 byte = 0b1010_1100;
    uint8 reversed = 0;

    for int i in (0..8) {
        reversed = (reversed << 1) | (byte & 1);
        byte = byte >> 1;
    }

    println(reversed);
    return 0;
}
