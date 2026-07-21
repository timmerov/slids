/*
test implementation of expressions.

ensure we parse non-int types correctly.
simplify to mono-type expressions.

unary: + - ~ !
binary:
    math: + - * / %
    bitwise: & | ^ << >>
    logical: && || ^^
    comparison: == != >= <= > <

precedence, parentheses, ...

! and comparisons consume condition-expressions.
0-like values (false, 0, 0.0, nullptr) are false.
everything else is true.

pre/post increment/decrement not tested here.
the ppid rule has implications for parameters.

augmented assignments: += -= *= /= %= &= |= ^= <<= >>= &&= ||= ^^=
augmented assignments are handled in the desugar stage.
*/

/*
claude says:

One mono-typed block per type (bool, char, int8..int64, uint8..uint64, intptr,
float32/64). char is unsigned i8 printed as a glyph; float shifts lower to
mul/div by 2^rhs; float bitwise (& | ^) is a hard error (the negative markers).

Coverage added this pass: signed-observable int8 (negative operands -> sdiv/
srem/ashr) and unsigned-observable uint8 (high-bit 200 -> udiv/urem/lshr/ugt),
plus char ~ via (int=~cA) -> 178 (proves char zero-extends).

Open: for widths OTHER than int8/uint8 the signed-vs-unsigned op choice is
output-invisible (every operand is small + positive) — verified correct in the
IR but not locked by output. Add a negative / high-bit case per width to pin it.
*/

int32 main() {

    // bool — only logical, equality, and !
    bool bA = true;
    bool bB = false;
    bool bNot  = !bA;        __println("bNot= "  + bNot);
    bool bAnd  = bA && bB;   __println("bAnd= "  + bAnd);
    bool bOr   = bA || bB;   __println("bOr= "   + bOr);
    bool bXor  = bA ^^ bB;   __println("bXor= "  + bXor);
    bool bEq   = bA == bB;   __println("bEq= "   + bEq);
    bool bNe   = bA != bB;   __println("bNe= "   + bNe);

    // char — bitwise / comparison / logical / ! (skip arith + shifts; output is glyph)
    char cA = 'M';
    char cB = 'm';
    char cAnd  = cA & cB;    __println("cAnd= "  + cAnd);
    char cOr   = cA | cB;    __println("cOr= "   + cOr);
    char cXor  = cA ^ cB;    __println("cXor= "  + cXor);
    bool cEq   = cA == cB;   __println("cEq= "   + cEq);
    bool cNe   = cA != cB;   __println("cNe= "   + cNe);
    bool cLt   = cA < cB;    __println("cLt= "   + cLt);
    bool cLe   = cA <= cB;   __println("cLe= "   + cLe);
    bool cGt   = cA > cB;    __println("cGt= "   + cGt);
    bool cGe   = cA >= cB;   __println("cGe= "   + cGe);
    bool cLAnd = cA && cB;   __println("cLAnd= " + cLAnd);
    bool cLOr  = cA || cB;   __println("cLOr= "  + cLOr);
    bool cLXor = cA ^^ cB;   __println("cLXor= " + cLXor);
    bool cLNot = !cA;        __println("cLNot= " + cLNot);
    int  cBNot = (int=~cA);  __println("cBNot= " + cBNot);

    // int8
    int8 i8A = 12;
    int8 i8B = 5;
    int8 i8Add = i8A + i8B;  __println("i8Add= " + i8Add);
    int8 i8Sub = i8A - i8B;  __println("i8Sub= " + i8Sub);
    int8 i8Mul = i8A * i8B;  __println("i8Mul= " + i8Mul);
    int8 i8Div = i8A / i8B;  __println("i8Div= " + i8Div);
    int8 i8Mod = i8A % i8B;  __println("i8Mod= " + i8Mod);
    int8 i8And = i8A & i8B;  __println("i8And= " + i8And);
    int8 i8Or  = i8A | i8B;  __println("i8Or= "  + i8Or);
    int8 i8Xor = i8A ^ i8B;  __println("i8Xor= " + i8Xor);
    int8 i8Shl = i8A << 1;   __println("i8Shl= " + i8Shl);
    int8 i8Shr = i8A >> 1;   __println("i8Shr= " + i8Shr);
    int8 i8Pos = +i8A;       __println("i8Pos= " + i8Pos);
    int8 i8Neg = -i8A;       __println("i8Neg= " + i8Neg);
    int8 i8BNot = ~i8A;      __println("i8BNot= " + i8BNot);
    bool i8Eq   = i8A == i8B; __println("i8Eq= "  + i8Eq);
    bool i8Ne   = i8A != i8B; __println("i8Ne= "  + i8Ne);
    bool i8Lt   = i8A < i8B;  __println("i8Lt= "  + i8Lt);
    bool i8Le   = i8A <= i8B; __println("i8Le= "  + i8Le);
    bool i8Gt   = i8A > i8B;  __println("i8Gt= "  + i8Gt);
    bool i8Ge   = i8A >= i8B; __println("i8Ge= "  + i8Ge);
    bool i8LAnd = i8A && i8B; __println("i8LAnd= " + i8LAnd);
    bool i8LOr  = i8A || i8B; __println("i8LOr= "  + i8LOr);
    bool i8LXor = i8A ^^ i8B; __println("i8LXor= " + i8LXor);
    bool i8LNot = !i8A;       __println("i8LNot= " + i8LNot);
    // signed-observable: negative operands distinguish sdiv/srem/ashr/slt from unsigned.
    int8 i8N = -7;
    int8 i8sDiv = i8N / 2;   __println("i8sDiv= " + i8sDiv);
    int8 i8sRem = i8N % 2;   __println("i8sRem= " + i8sRem);
    int8 i8sShr = i8N >> 1;  __println("i8sShr= " + i8sShr);
    bool i8sLt  = i8N < i8B; __println("i8sLt= "  + i8sLt);

    // int16
    int16 i16A = 12;
    int16 i16B = 5;
    int16 i16Add = i16A + i16B;  __println("i16Add= " + i16Add);
    int16 i16Sub = i16A - i16B;  __println("i16Sub= " + i16Sub);
    int16 i16Mul = i16A * i16B;  __println("i16Mul= " + i16Mul);
    int16 i16Div = i16A / i16B;  __println("i16Div= " + i16Div);
    int16 i16Mod = i16A % i16B;  __println("i16Mod= " + i16Mod);
    int16 i16And = i16A & i16B;  __println("i16And= " + i16And);
    int16 i16Or  = i16A | i16B;  __println("i16Or= "  + i16Or);
    int16 i16Xor = i16A ^ i16B;  __println("i16Xor= " + i16Xor);
    int16 i16Shl = i16A << 1;    __println("i16Shl= " + i16Shl);
    int16 i16Shr = i16A >> 1;    __println("i16Shr= " + i16Shr);
    int16 i16Pos = +i16A;        __println("i16Pos= " + i16Pos);
    int16 i16Neg = -i16A;        __println("i16Neg= " + i16Neg);
    int16 i16BNot = ~i16A;       __println("i16BNot= " + i16BNot);
    bool i16Eq   = i16A == i16B; __println("i16Eq= "  + i16Eq);
    bool i16Ne   = i16A != i16B; __println("i16Ne= "  + i16Ne);
    bool i16Lt   = i16A < i16B;  __println("i16Lt= "  + i16Lt);
    bool i16Le   = i16A <= i16B; __println("i16Le= "  + i16Le);
    bool i16Gt   = i16A > i16B;  __println("i16Gt= "  + i16Gt);
    bool i16Ge   = i16A >= i16B; __println("i16Ge= "  + i16Ge);
    bool i16LAnd = i16A && i16B; __println("i16LAnd= " + i16LAnd);
    bool i16LOr  = i16A || i16B; __println("i16LOr= "  + i16LOr);
    bool i16LXor = i16A ^^ i16B; __println("i16LXor= " + i16LXor);
    bool i16LNot = !i16A;        __println("i16LNot= " + i16LNot);

    // int32
    int32 i32A = 12;
    int32 i32B = 5;
    int32 i32Add = i32A + i32B;  __println("i32Add= " + i32Add);
    int32 i32Sub = i32A - i32B;  __println("i32Sub= " + i32Sub);
    int32 i32Mul = i32A * i32B;  __println("i32Mul= " + i32Mul);
    int32 i32Div = i32A / i32B;  __println("i32Div= " + i32Div);
    int32 i32Mod = i32A % i32B;  __println("i32Mod= " + i32Mod);
    int32 i32And = i32A & i32B;  __println("i32And= " + i32And);
    int32 i32Or  = i32A | i32B;  __println("i32Or= "  + i32Or);
    int32 i32Xor = i32A ^ i32B;  __println("i32Xor= " + i32Xor);
    int32 i32Shl = i32A << 1;    __println("i32Shl= " + i32Shl);
    int32 i32Shr = i32A >> 1;    __println("i32Shr= " + i32Shr);
    int32 i32Pos = +i32A;        __println("i32Pos= " + i32Pos);
    int32 i32Neg = -i32A;        __println("i32Neg= " + i32Neg);
    int32 i32BNot = ~i32A;       __println("i32BNot= " + i32BNot);
    bool i32Eq   = i32A == i32B; __println("i32Eq= "  + i32Eq);
    bool i32Ne   = i32A != i32B; __println("i32Ne= "  + i32Ne);
    bool i32Lt   = i32A < i32B;  __println("i32Lt= "  + i32Lt);
    bool i32Le   = i32A <= i32B; __println("i32Le= "  + i32Le);
    bool i32Gt   = i32A > i32B;  __println("i32Gt= "  + i32Gt);
    bool i32Ge   = i32A >= i32B; __println("i32Ge= "  + i32Ge);
    bool i32LAnd = i32A && i32B; __println("i32LAnd= " + i32LAnd);
    bool i32LOr  = i32A || i32B; __println("i32LOr= "  + i32LOr);
    bool i32LXor = i32A ^^ i32B; __println("i32LXor= " + i32LXor);
    bool i32LNot = !i32A;        __println("i32LNot= " + i32LNot);

    // int64
    int64 i64A = 12;
    int64 i64B = 5;
    int64 i64Add = i64A + i64B;  __println("i64Add= " + i64Add);
    int64 i64Sub = i64A - i64B;  __println("i64Sub= " + i64Sub);
    int64 i64Mul = i64A * i64B;  __println("i64Mul= " + i64Mul);
    int64 i64Div = i64A / i64B;  __println("i64Div= " + i64Div);
    int64 i64Mod = i64A % i64B;  __println("i64Mod= " + i64Mod);
    int64 i64And = i64A & i64B;  __println("i64And= " + i64And);
    int64 i64Or  = i64A | i64B;  __println("i64Or= "  + i64Or);
    int64 i64Xor = i64A ^ i64B;  __println("i64Xor= " + i64Xor);
    int64 i64Shl = i64A << 1;    __println("i64Shl= " + i64Shl);
    int64 i64Shr = i64A >> 1;    __println("i64Shr= " + i64Shr);
    int64 i64Pos = +i64A;        __println("i64Pos= " + i64Pos);
    int64 i64Neg = -i64A;        __println("i64Neg= " + i64Neg);
    int64 i64BNot = ~i64A;       __println("i64BNot= " + i64BNot);
    bool i64Eq   = i64A == i64B; __println("i64Eq= "  + i64Eq);
    bool i64Ne   = i64A != i64B; __println("i64Ne= "  + i64Ne);
    bool i64Lt   = i64A < i64B;  __println("i64Lt= "  + i64Lt);
    bool i64Le   = i64A <= i64B; __println("i64Le= "  + i64Le);
    bool i64Gt   = i64A > i64B;  __println("i64Gt= "  + i64Gt);
    bool i64Ge   = i64A >= i64B; __println("i64Ge= "  + i64Ge);
    bool i64LAnd = i64A && i64B; __println("i64LAnd= " + i64LAnd);
    bool i64LOr  = i64A || i64B; __println("i64LOr= "  + i64LOr);
    bool i64LXor = i64A ^^ i64B; __println("i64LXor= " + i64LXor);
    bool i64LNot = !i64A;        __println("i64LNot= " + i64LNot);

    // uint8 (no unary -)
    uint8 u8A = 12;
    uint8 u8B = 5;
    uint8 u8Add = u8A + u8B;  __println("u8Add= " + u8Add);
    uint8 u8Sub = u8A - u8B;  __println("u8Sub= " + u8Sub);
    uint8 u8Mul = u8A * u8B;  __println("u8Mul= " + u8Mul);
    uint8 u8Div = u8A / u8B;  __println("u8Div= " + u8Div);
    uint8 u8Mod = u8A % u8B;  __println("u8Mod= " + u8Mod);
    uint8 u8And = u8A & u8B;  __println("u8And= " + u8And);
    uint8 u8Or  = u8A | u8B;  __println("u8Or= "  + u8Or);
    uint8 u8Xor = u8A ^ u8B;  __println("u8Xor= " + u8Xor);
    uint8 u8Shl = u8A << 1;   __println("u8Shl= " + u8Shl);
    uint8 u8Shr = u8A >> 1;   __println("u8Shr= " + u8Shr);
    uint8 u8Pos = +u8A;       __println("u8Pos= " + u8Pos);
    uint8 u8BNot = ~u8A;      __println("u8BNot= " + u8BNot);
    bool u8Eq   = u8A == u8B; __println("u8Eq= "  + u8Eq);
    bool u8Ne   = u8A != u8B; __println("u8Ne= "  + u8Ne);
    bool u8Lt   = u8A < u8B;  __println("u8Lt= "  + u8Lt);
    bool u8Le   = u8A <= u8B; __println("u8Le= "  + u8Le);
    bool u8Gt   = u8A > u8B;  __println("u8Gt= "  + u8Gt);
    bool u8Ge   = u8A >= u8B; __println("u8Ge= "  + u8Ge);
    bool u8LAnd = u8A && u8B; __println("u8LAnd= " + u8LAnd);
    bool u8LOr  = u8A || u8B; __println("u8LOr= "  + u8LOr);
    bool u8LXor = u8A ^^ u8B; __println("u8LXor= " + u8LXor);
    bool u8LNot = !u8A;       __println("u8LNot= " + u8LNot);
    // unsigned-observable: high-bit operands distinguish udiv/urem/lshr/ugt from signed.
    uint8 u8H = 200;
    uint8 u8uDiv = u8H / 3;   __println("u8uDiv= " + u8uDiv);
    uint8 u8uRem = u8H % 3;   __println("u8uRem= " + u8uRem);
    uint8 u8uShr = u8H >> 1;  __println("u8uShr= " + u8uShr);
    bool  u8uGt  = u8H > u8B; __println("u8uGt= "  + u8uGt);

    // uint16
    uint16 u16A = 12;
    uint16 u16B = 5;
    uint16 u16Add = u16A + u16B;  __println("u16Add= " + u16Add);
    uint16 u16Sub = u16A - u16B;  __println("u16Sub= " + u16Sub);
    uint16 u16Mul = u16A * u16B;  __println("u16Mul= " + u16Mul);
    uint16 u16Div = u16A / u16B;  __println("u16Div= " + u16Div);
    uint16 u16Mod = u16A % u16B;  __println("u16Mod= " + u16Mod);
    uint16 u16And = u16A & u16B;  __println("u16And= " + u16And);
    uint16 u16Or  = u16A | u16B;  __println("u16Or= "  + u16Or);
    uint16 u16Xor = u16A ^ u16B;  __println("u16Xor= " + u16Xor);
    uint16 u16Shl = u16A << 1;    __println("u16Shl= " + u16Shl);
    uint16 u16Shr = u16A >> 1;    __println("u16Shr= " + u16Shr);
    uint16 u16Pos = +u16A;        __println("u16Pos= " + u16Pos);
    uint16 u16BNot = ~u16A;       __println("u16BNot= " + u16BNot);
    bool u16Eq   = u16A == u16B; __println("u16Eq= "  + u16Eq);
    bool u16Ne   = u16A != u16B; __println("u16Ne= "  + u16Ne);
    bool u16Lt   = u16A < u16B;  __println("u16Lt= "  + u16Lt);
    bool u16Le   = u16A <= u16B; __println("u16Le= "  + u16Le);
    bool u16Gt   = u16A > u16B;  __println("u16Gt= "  + u16Gt);
    bool u16Ge   = u16A >= u16B; __println("u16Ge= "  + u16Ge);
    bool u16LAnd = u16A && u16B; __println("u16LAnd= " + u16LAnd);
    bool u16LOr  = u16A || u16B; __println("u16LOr= "  + u16LOr);
    bool u16LXor = u16A ^^ u16B; __println("u16LXor= " + u16LXor);
    bool u16LNot = !u16A;        __println("u16LNot= " + u16LNot);

    // uint32
    uint32 u32A = 12;
    uint32 u32B = 5;
    uint32 u32Add = u32A + u32B;  __println("u32Add= " + u32Add);
    uint32 u32Sub = u32A - u32B;  __println("u32Sub= " + u32Sub);
    uint32 u32Mul = u32A * u32B;  __println("u32Mul= " + u32Mul);
    uint32 u32Div = u32A / u32B;  __println("u32Div= " + u32Div);
    uint32 u32Mod = u32A % u32B;  __println("u32Mod= " + u32Mod);
    uint32 u32And = u32A & u32B;  __println("u32And= " + u32And);
    uint32 u32Or  = u32A | u32B;  __println("u32Or= "  + u32Or);
    uint32 u32Xor = u32A ^ u32B;  __println("u32Xor= " + u32Xor);
    uint32 u32Shl = u32A << 1;    __println("u32Shl= " + u32Shl);
    uint32 u32Shr = u32A >> 1;    __println("u32Shr= " + u32Shr);
    uint32 u32Pos = +u32A;        __println("u32Pos= " + u32Pos);
    uint32 u32BNot = ~u32A;       __println("u32BNot= " + u32BNot);
    bool u32Eq   = u32A == u32B; __println("u32Eq= "  + u32Eq);
    bool u32Ne   = u32A != u32B; __println("u32Ne= "  + u32Ne);
    bool u32Lt   = u32A < u32B;  __println("u32Lt= "  + u32Lt);
    bool u32Le   = u32A <= u32B; __println("u32Le= "  + u32Le);
    bool u32Gt   = u32A > u32B;  __println("u32Gt= "  + u32Gt);
    bool u32Ge   = u32A >= u32B; __println("u32Ge= "  + u32Ge);
    bool u32LAnd = u32A && u32B; __println("u32LAnd= " + u32LAnd);
    bool u32LOr  = u32A || u32B; __println("u32LOr= "  + u32LOr);
    bool u32LXor = u32A ^^ u32B; __println("u32LXor= " + u32LXor);
    bool u32LNot = !u32A;        __println("u32LNot= " + u32LNot);

    // uint64
    uint64 u64A = 12;
    uint64 u64B = 5;
    uint64 u64Add = u64A + u64B;  __println("u64Add= " + u64Add);
    uint64 u64Sub = u64A - u64B;  __println("u64Sub= " + u64Sub);
    uint64 u64Mul = u64A * u64B;  __println("u64Mul= " + u64Mul);
    uint64 u64Div = u64A / u64B;  __println("u64Div= " + u64Div);
    uint64 u64Mod = u64A % u64B;  __println("u64Mod= " + u64Mod);
    uint64 u64And = u64A & u64B;  __println("u64And= " + u64And);
    uint64 u64Or  = u64A | u64B;  __println("u64Or= "  + u64Or);
    uint64 u64Xor = u64A ^ u64B;  __println("u64Xor= " + u64Xor);
    uint64 u64Shl = u64A << 1;    __println("u64Shl= " + u64Shl);
    uint64 u64Shr = u64A >> 1;    __println("u64Shr= " + u64Shr);
    uint64 u64Pos = +u64A;        __println("u64Pos= " + u64Pos);
    uint64 u64BNot = ~u64A;       __println("u64BNot= " + u64BNot);
    bool u64Eq   = u64A == u64B; __println("u64Eq= "  + u64Eq);
    bool u64Ne   = u64A != u64B; __println("u64Ne= "  + u64Ne);
    bool u64Lt   = u64A < u64B;  __println("u64Lt= "  + u64Lt);
    bool u64Le   = u64A <= u64B; __println("u64Le= "  + u64Le);
    bool u64Gt   = u64A > u64B;  __println("u64Gt= "  + u64Gt);
    bool u64Ge   = u64A >= u64B; __println("u64Ge= "  + u64Ge);
    bool u64LAnd = u64A && u64B; __println("u64LAnd= " + u64LAnd);
    bool u64LOr  = u64A || u64B; __println("u64LOr= "  + u64LOr);
    bool u64LXor = u64A ^^ u64B; __println("u64LXor= " + u64LXor);
    bool u64LNot = !u64A;        __println("u64LNot= " + u64LNot);

    // uint64 large value — exercises the %llu print fix
    uint64 u64Big = 18_446_744_073_709_551_613;
    __println("u64Big= " + u64Big);

    // intptr (typically i64 signed)
    intptr ipA = 12;
    intptr ipB = 5;
    intptr ipAdd = ipA + ipB;  __println("ipAdd= " + ipAdd);
    intptr ipSub = ipA - ipB;  __println("ipSub= " + ipSub);
    intptr ipMul = ipA * ipB;  __println("ipMul= " + ipMul);
    intptr ipDiv = ipA / ipB;  __println("ipDiv= " + ipDiv);
    intptr ipMod = ipA % ipB;  __println("ipMod= " + ipMod);
    intptr ipAnd = ipA & ipB;  __println("ipAnd= " + ipAnd);
    intptr ipOr  = ipA | ipB;  __println("ipOr= "  + ipOr);
    intptr ipXor = ipA ^ ipB;  __println("ipXor= " + ipXor);
    intptr ipShl = ipA << 1;   __println("ipShl= " + ipShl);
    intptr ipShr = ipA >> 1;   __println("ipShr= " + ipShr);
    intptr ipPos = +ipA;       __println("ipPos= " + ipPos);
    intptr ipNeg = -ipA;       __println("ipNeg= " + ipNeg);
    intptr ipBNot = ~ipA;      __println("ipBNot= " + ipBNot);
    bool ipEq   = ipA == ipB;  __println("ipEq= "  + ipEq);
    bool ipNe   = ipA != ipB;  __println("ipNe= "  + ipNe);
    bool ipLt   = ipA < ipB;   __println("ipLt= "  + ipLt);
    bool ipLe   = ipA <= ipB;  __println("ipLe= "  + ipLe);
    bool ipGt   = ipA > ipB;   __println("ipGt= "  + ipGt);
    bool ipGe   = ipA >= ipB;  __println("ipGe= "  + ipGe);
    bool ipLAnd = ipA && ipB;  __println("ipLAnd= " + ipLAnd);
    bool ipLOr  = ipA || ipB;  __println("ipLOr= "  + ipLOr);
    bool ipLXor = ipA ^^ ipB;  __println("ipLXor= " + ipLXor);
    bool ipLNot = !ipA;        __println("ipLNot= " + ipLNot);

    // float32 — math (incl. %) / comparison / logical / unary (no ~, no bitwise, no shift)
    float32 f32A = 12.5;
    float32 f32B = 2.5;
    float32 f32Add = f32A + f32B;  __println("f32Add= " + f32Add);
    float32 f32Sub = f32A - f32B;  __println("f32Sub= " + f32Sub);
    float32 f32Mul = f32A * f32B;  __println("f32Mul= " + f32Mul);
    float32 f32Div = f32A / f32B;  __println("f32Div= " + f32Div);
    float32 f32Mod = f32A % f32B;  __println("f32Mod= " + f32Mod);
    float32 f32Pos = +f32A;        __println("f32Pos= " + f32Pos);
    float32 f32Neg = -f32A;        __println("f32Neg= " + f32Neg);
    bool f32Eq   = f32A == f32B;   __println("f32Eq= "  + f32Eq);
    bool f32Ne   = f32A != f32B;   __println("f32Ne= "  + f32Ne);
    bool f32Lt   = f32A < f32B;    __println("f32Lt= "  + f32Lt);
    bool f32Le   = f32A <= f32B;   __println("f32Le= "  + f32Le);
    bool f32Gt   = f32A > f32B;    __println("f32Gt= "  + f32Gt);
    bool f32Ge   = f32A >= f32B;   __println("f32Ge= "  + f32Ge);
    bool f32LAnd = f32A && f32B;   __println("f32LAnd= " + f32LAnd);
    bool f32LOr  = f32A || f32B;   __println("f32LOr= "  + f32LOr);
    bool f32LXor = f32A ^^ f32B;   __println("f32LXor= " + f32LXor);
    bool f32LNot = !f32A;          __println("f32LNot= " + f32LNot);
    // float32 shifts — lhs * (1<<rhs) and lhs / (1<<rhs).
    float32 f32Shl = f32A << 1;    __println("f32Shl= " + f32Shl);
    float32 f32Shr = f32A >> 1;    __println("f32Shr= " + f32Shr);
    // float32 negatives — bitwise not defined on floating-point.
    // (each reads its local so the type error surfaces ahead of the unused check.)
    //-EXPECT-ERROR: Bitwise '&' not defined on floating-point type 'float32'.
    // float32 f32And = f32A & f32B;
    // __println("f32And= " + f32And);
    //-EXPECT-ERROR: Bitwise '|' not defined on floating-point type 'float32'.
    // float32 f32Or  = f32A | f32B;
    // __println("f32Or= " + f32Or);
    //-EXPECT-ERROR: Bitwise '^' not defined on floating-point type 'float32'.
    // float32 f32Xor = f32A ^ f32B;
    // __println("f32Xor= " + f32Xor);

    // float64
    float64 f64A = 12.5;
    float64 f64B = 2.5;
    float64 f64Add = f64A + f64B;  __println("f64Add= " + f64Add);
    float64 f64Sub = f64A - f64B;  __println("f64Sub= " + f64Sub);
    float64 f64Mul = f64A * f64B;  __println("f64Mul= " + f64Mul);
    float64 f64Div = f64A / f64B;  __println("f64Div= " + f64Div);
    float64 f64Mod = f64A % f64B;  __println("f64Mod= " + f64Mod);
    float64 f64Pos = +f64A;        __println("f64Pos= " + f64Pos);
    float64 f64Neg = -f64A;        __println("f64Neg= " + f64Neg);
    bool f64Eq   = f64A == f64B;   __println("f64Eq= "  + f64Eq);
    bool f64Ne   = f64A != f64B;   __println("f64Ne= "  + f64Ne);
    bool f64Lt   = f64A < f64B;    __println("f64Lt= "  + f64Lt);
    bool f64Le   = f64A <= f64B;   __println("f64Le= "  + f64Le);
    bool f64Gt   = f64A > f64B;    __println("f64Gt= "  + f64Gt);
    bool f64Ge   = f64A >= f64B;   __println("f64Ge= "  + f64Ge);
    bool f64LAnd = f64A && f64B;   __println("f64LAnd= " + f64LAnd);
    bool f64LOr  = f64A || f64B;   __println("f64LOr= "  + f64LOr);
    bool f64LXor = f64A ^^ f64B;   __println("f64LXor= " + f64LXor);
    bool f64LNot = !f64A;          __println("f64LNot= " + f64LNot);
    // float64 shifts — lhs * (1<<rhs) and lhs / (1<<rhs).
    float64 f64Shl = f64A << 1;    __println("f64Shl= " + f64Shl);
    float64 f64Shr = f64A >> 1;    __println("f64Shr= " + f64Shr);
    // float64 negatives — bitwise not defined on floating-point.
    // (each reads its local so the type error surfaces ahead of the unused check.)
    //-EXPECT-ERROR: Bitwise '&' not defined on floating-point type 'float64'.
    // float64 f64And = f64A & f64B;
    // __println("f64And= " + f64And);
    //-EXPECT-ERROR: Bitwise '|' not defined on floating-point type 'float64'.
    // float64 f64Or  = f64A | f64B;
    // __println("f64Or= " + f64Or);
    //-EXPECT-ERROR: Bitwise '^' not defined on floating-point type 'float64'.
    // float64 f64Xor = f64A ^ f64B;
    // __println("f64Xor= " + f64Xor);

    /* --- Type(value): non-int primitive temporaries, bound by the DECL-INIT rules --- */
    __println("tf32= "  + float32(1.5));
    __println("tf64= "  + float64(2.5));
    __println("tflt= "  + float(3.5));
    __println("tbool= " + bool(true));
    __println("tchar= " + char(65));
    float32 twf = 1.25;
    __println("twiden= " + float64(twf));   // float32 source widens into float64

    /* --- Type(value) negatives: one //-block uncommented per run --- */

    //-EXPECT-ERROR: Cannot implicitly convert 'int' to 'float64'
    // float64 tcf = float64(5);
    // __println("tcf= " + tcf);

    //-EXPECT-ERROR: Cannot implicitly narrow 'float64' to 'float32'
    // float64 twd = 1.5;
    // float32 tnf = float32(twd);
    // __println("tnf= " + tnf);

    //-EXPECT-ERROR: A primitive temporary 'Type(value)' requires exactly one value.
    // float64 tz = float64();
    // __println("tz= " + tz);

    return 0;
}
