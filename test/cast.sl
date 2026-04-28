
int32 main() {
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
    __println("intptr=" + addr + " round-trip:   ok (no crash)");

    // float bit reinterpretation through void^ (two explicit casts required)
    // IEEE 754: float32 1.0 = 0x3F800000 = 1065353216
    float32 one      = (float32=1.0);
    int32   one_bits = (<int32^> <void^> ^one)^;
    __println("bits of 1.0f:        expected 1065353216, got " + one_bits);

    /* test compile errors. */
    int16^ p16 = nullptr;
    int32^ p32 = nullptr;
    intptr intp = 0;
    /* compile error */
    //p16 = p32;
    /* compile error */
    //p16 = <int16^> p32;
    /* compile error */
    //p16 = intp;
    /* correct usage. */
    p16 = <int16^> <void^> p32;
    p16 = <int16^> <int8^> p32;
    p16 = <int16^> intp;
    /* this generates invalid ll */
    intp = p16;

    return 0;
}
