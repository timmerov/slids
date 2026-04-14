/*
type_infer.sl - demonstrates strong type inference for variable declarations.
x = 0; declares x as int (inferred).
*/

int main() {
    // decimal integers: int if fits in 32 bits, int64 if larger
    a = 0;
    b = 100;
    c = 3_000_000_000;   // int64: too big for int32

    // hex/binary: uint (uint32) or uint64
    d = 0xFF;
    e = 0b1010_1010;
    f = 0x1_0000_0000;   // uint64: doesn't fit in uint32

    // float
    g = 3.14;

    // reassignment works: a is still int
    a = 42;

    __println(a);
    __println(b);
    __println(c);
    __println(d);
    __println(e);
    __println(f);
    __println(g);
    return 0;
}
