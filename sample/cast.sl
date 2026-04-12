
int32 main() {
    // -------------------------------------------------------
    // Numeric casts — integer
    // -------------------------------------------------------

    // narrowing: 300 truncated to i8 = 44  (300 - 256 = 44)
    int32 a = 300;
    int8  b = int8(a);
    __println("int8(300):           expected 44,  got " + b);

    // widening signed: sign-extend -50 from i8 to i32
    int8  c = int8(206);        // 206 as i8 = -50  (206 - 256 = -50)
    int32 d = int32(c);
    __println("int32(int8 -50):     expected -50, got " + d);

    // widening unsigned: zero-extend 200 from u8 to u32
    uint8  e = uint8(200);
    uint32 f = uint32(e);
    __println("uint32(uint8 200):   expected 200, got " + f);

    // sign reinterpretation: same bits, different type — no instruction emitted
    int32  g = 65535;
    uint32 h = uint32(g);
    __println("uint32(int32 65535): expected 65535, got " + h);

    // -------------------------------------------------------
    // Numeric casts — float
    // -------------------------------------------------------

    // float literal stored directly as float64
    float64 pi = 3.14159;
    __println("float64 3.14159:     " + pi);

    // float to int (truncates toward zero)
    int32 pi_int = int32(pi);
    __println("int32(3.14159):      expected 3,   got " + pi_int);

    // int to float
    int32 n = 42;
    float64 fn = float64(n);
    __println("float64(42):         " + fn);

    // float32 from double literal (fptrunc)
    float32 f32 = float32(2.0);
    __println("float32(2.0):        " + f32);

    // float32 → float64 (fpext)
    float64 f64 = float64(f32);
    __println("float64(float32 2.0):" + f64);

    // -------------------------------------------------------
    // Pointer reinterpretation casts
    // -------------------------------------------------------

    // nullptr (void^) assigned to any pointer type — no explicit cast needed
    int32^ null_ptr = nullptr;
    __println("nullptr → int32^:    ok (no crash)");

    // any pointer implicitly converts to void^ (both are ptr in LLVM — no cast needed)
    int8[] buf = new int8[8];
    void^ raw = buf;
    __println("int8[] → void^:      ok");

    // void^ implicitly converts to any pointer
    int8[] back = raw;
    delete buf;
    __println("void^ → int8[]:      ok");

    // explicit <void^> and back — round-trip a value through void^
    int32 val = 99;
    int32^ vp   = ^val;
    void^  vraw = <void^> vp;
    int32^ vrp  = <int32^> vraw;
    int32  recovered = vrp^;
    __println("round-trip void^:    expected 99, got " + recovered);

    // pointer ↔ intptr
    int8[] ibuf  = new int8[4];
    intptr addr  = <intptr> ibuf;
    int8[] ibuf2 = <int8[]> addr;
    delete ibuf;
    __println("intptr round-trip:   ok (no crash)");

    // float bit reinterpretation through void^ (two explicit casts required)
    // IEEE 754: float32 1.0 = 0x3F800000 = 1065353216
    float32 one      = float32(1.0);
    int32   one_bits = (<int32^> <void^> ^one)^;
    __println("bits of 1.0f:        expected 1065353216, got " + one_bits);

    return 0;
}
