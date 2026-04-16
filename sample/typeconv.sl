
// A simple class that wraps an int32.
Value(int32 n_ = 0) {
    op=(int32 n);
    op=(Value^ v);
    int32 get();
}

Value {
    // convert from int32
    op=(int32 n) {
        n_ = n;
    }

    // copy from another Value
    op=(Value^ v) {
        n_ = v^.n_;
    }

    int32 get() {
        return n_;
    }
}


int32 main() {
    // -------------------------------------------------------
    // Integer type conversions
    // -------------------------------------------------------

    // narrowing: 300 truncated to i8 = 44  (300 - 256 = 44)
    int32 a = 300;
    int8  b = (int8=a);
    __println("int8(300):           expected 44,  got " + b);

    // widening signed: sign-extend -50 from i8 to i32
    int8  c = (int8=206);        // 206 as i8 = -50  (206 - 256 = -50)
    int32 d = (int32=c);
    __println("int32(int8 -50):     expected -50, got " + d);

    // widening unsigned: zero-extend 200 from u8 to u32
    uint8  e = (uint8=200);
    uint32 f = (uint32=e);
    __println("uint32(uint8 200):   expected 200, got " + f);

    // sign reinterpretation: same bits, different type — no instruction emitted
    int32  g = 65535;
    uint32 h = (uint32=g);
    __println("uint32(int32 65535): expected 65535, got " + h);

    // -------------------------------------------------------
    // Float type conversions
    // -------------------------------------------------------

    // float literal stored directly as float64
    float64 pi = 3.14159;
    __println("float64 3.14159:     " + pi);

    // float to int (truncates toward zero)
    int32 pi_int = (int32=pi);
    __println("int32(3.14159):      expected 3,   got " + pi_int);

    // int to float
    int32 n = 42;
    float64 fn = (float64=n);
    __println("float64(42):         " + fn);

    // float32 from double literal (fptrunc)
    float32 f32 = (float32=2.0);
    __println("float32(2.0):        " + f32);

    // float32 → float64 (fpext)
    float64 f64 = (float64=f32);
    __println("float64(float32 2.0):" + f64);

    // -------------------------------------------------------
    // Class type conversion
    // -------------------------------------------------------

    // (Value=42) — type conversion expression creates a Value from an int32 literal
    Value v = (Value=42);
    __println("Value(42):            expected 42,  got " + v.get());

    // (Value=d) — d is int32 -50 from above; same conversion, variable source
    Value w = (Value=d);
    __println("Value(-50):           expected -50, got " + w.get());

    return 0;
}
