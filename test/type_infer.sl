/*
type_infer.sl - demonstrates strong type inference for variable declarations.
x = 0; declares x as int (inferred).
*/

intptr strlen_helper(char[] s) {
    intptr len = 0;
    while () {
        char ch = s^;
        if (ch == 0) { break; }
        ++s;
        ++len;
    }
    return len;
}

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

    // pointer: new T[n] → T[]
    buf = new char[16];
    buf[0] = 'H'; buf[1] = 'i'; buf[2] = 0;

    // function call return type: intptr
    slen = strlen_helper(buf);

    // address-of: ^x → int^
    int local = 99;
    ref = ^local;
    ref^ = 77;

    // reassignment works: a is still int
    a = 42;

    __println(a);        // 42
    __println(b);        // 100
    __println(c);        // 3000000000
    __println(d);        // 255
    __println(e);        // 170
    __println(f);        // 4294967296
    __println(g);        // 3.140000
    __println(buf);      // Hi
    __println(slen);     // 2
    __println(local);    // 77
    delete buf;
    return 0;
}
