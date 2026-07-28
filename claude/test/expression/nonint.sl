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

import string;

int32 main() {

    // bool — only logical, equality, and !
    bool bA = true;
    bool bB = false;
    bool bNot  = !bA;        println(String + "bNot= "  + bNot);
    bool bAnd  = bA && bB;   println(String + "bAnd= "  + bAnd);
    bool bOr   = bA || bB;   println(String + "bOr= "   + bOr);
    bool bXor  = bA ^^ bB;   println(String + "bXor= "  + bXor);
    bool bEq   = bA == bB;   println(String + "bEq= "   + bEq);
    bool bNe   = bA != bB;   println(String + "bNe= "   + bNe);

    // char — bitwise / comparison / logical / ! (skip arith + shifts; output is glyph)
    char cA = 'M';
    char cB = 'm';
    char cAnd  = cA & cB;    println(String + "cAnd= "  + cAnd);
    char cOr   = cA | cB;    println(String + "cOr= "   + cOr);
    char cXor  = cA ^ cB;    println(String + "cXor= "  + cXor);
    bool cEq   = cA == cB;   println(String + "cEq= "   + cEq);
    bool cNe   = cA != cB;   println(String + "cNe= "   + cNe);
    bool cLt   = cA < cB;    println(String + "cLt= "   + cLt);
    bool cLe   = cA <= cB;   println(String + "cLe= "   + cLe);
    bool cGt   = cA > cB;    println(String + "cGt= "   + cGt);
    bool cGe   = cA >= cB;   println(String + "cGe= "   + cGe);
    bool cLAnd = cA && cB;   println(String + "cLAnd= " + cLAnd);
    bool cLOr  = cA || cB;   println(String + "cLOr= "  + cLOr);
    bool cLXor = cA ^^ cB;   println(String + "cLXor= " + cLXor);
    bool cLNot = !cA;        println(String + "cLNot= " + cLNot);
    int  cBNot = (int=~cA);  println(String + "cBNot= " + cBNot);

    // int8
    int8 i8A = 12;
    int8 i8B = 5;
    int8 i8Add = i8A + i8B;  println(String + "i8Add= " + i8Add);
    int8 i8Sub = i8A - i8B;  println(String + "i8Sub= " + i8Sub);
    int8 i8Mul = i8A * i8B;  println(String + "i8Mul= " + i8Mul);
    int8 i8Div = i8A / i8B;  println(String + "i8Div= " + i8Div);
    int8 i8Mod = i8A % i8B;  println(String + "i8Mod= " + i8Mod);
    int8 i8And = i8A & i8B;  println(String + "i8And= " + i8And);
    int8 i8Or  = i8A | i8B;  println(String + "i8Or= "  + i8Or);
    int8 i8Xor = i8A ^ i8B;  println(String + "i8Xor= " + i8Xor);
    int8 i8Shl = i8A << 1;   println(String + "i8Shl= " + i8Shl);
    int8 i8Shr = i8A >> 1;   println(String + "i8Shr= " + i8Shr);
    int8 i8Pos = +i8A;       println(String + "i8Pos= " + i8Pos);
    int8 i8Neg = -i8A;       println(String + "i8Neg= " + i8Neg);
    int8 i8BNot = ~i8A;      println(String + "i8BNot= " + i8BNot);
    bool i8Eq   = i8A == i8B; println(String + "i8Eq= "  + i8Eq);
    bool i8Ne   = i8A != i8B; println(String + "i8Ne= "  + i8Ne);
    bool i8Lt   = i8A < i8B;  println(String + "i8Lt= "  + i8Lt);
    bool i8Le   = i8A <= i8B; println(String + "i8Le= "  + i8Le);
    bool i8Gt   = i8A > i8B;  println(String + "i8Gt= "  + i8Gt);
    bool i8Ge   = i8A >= i8B; println(String + "i8Ge= "  + i8Ge);
    bool i8LAnd = i8A && i8B; println(String + "i8LAnd= " + i8LAnd);
    bool i8LOr  = i8A || i8B; println(String + "i8LOr= "  + i8LOr);
    bool i8LXor = i8A ^^ i8B; println(String + "i8LXor= " + i8LXor);
    bool i8LNot = !i8A;       println(String + "i8LNot= " + i8LNot);
    // signed-observable: negative operands distinguish sdiv/srem/ashr/slt from unsigned.
    int8 i8N = -7;
    int8 i8sDiv = i8N / 2;   println(String + "i8sDiv= " + i8sDiv);
    int8 i8sRem = i8N % 2;   println(String + "i8sRem= " + i8sRem);
    int8 i8sShr = i8N >> 1;  println(String + "i8sShr= " + i8sShr);
    bool i8sLt  = i8N < i8B; println(String + "i8sLt= "  + i8sLt);

    // int16
    int16 i16A = 12;
    int16 i16B = 5;
    int16 i16Add = i16A + i16B;  println(String + "i16Add= " + i16Add);
    int16 i16Sub = i16A - i16B;  println(String + "i16Sub= " + i16Sub);
    int16 i16Mul = i16A * i16B;  println(String + "i16Mul= " + i16Mul);
    int16 i16Div = i16A / i16B;  println(String + "i16Div= " + i16Div);
    int16 i16Mod = i16A % i16B;  println(String + "i16Mod= " + i16Mod);
    int16 i16And = i16A & i16B;  println(String + "i16And= " + i16And);
    int16 i16Or  = i16A | i16B;  println(String + "i16Or= "  + i16Or);
    int16 i16Xor = i16A ^ i16B;  println(String + "i16Xor= " + i16Xor);
    int16 i16Shl = i16A << 1;    println(String + "i16Shl= " + i16Shl);
    int16 i16Shr = i16A >> 1;    println(String + "i16Shr= " + i16Shr);
    int16 i16Pos = +i16A;        println(String + "i16Pos= " + i16Pos);
    int16 i16Neg = -i16A;        println(String + "i16Neg= " + i16Neg);
    int16 i16BNot = ~i16A;       println(String + "i16BNot= " + i16BNot);
    bool i16Eq   = i16A == i16B; println(String + "i16Eq= "  + i16Eq);
    bool i16Ne   = i16A != i16B; println(String + "i16Ne= "  + i16Ne);
    bool i16Lt   = i16A < i16B;  println(String + "i16Lt= "  + i16Lt);
    bool i16Le   = i16A <= i16B; println(String + "i16Le= "  + i16Le);
    bool i16Gt   = i16A > i16B;  println(String + "i16Gt= "  + i16Gt);
    bool i16Ge   = i16A >= i16B; println(String + "i16Ge= "  + i16Ge);
    bool i16LAnd = i16A && i16B; println(String + "i16LAnd= " + i16LAnd);
    bool i16LOr  = i16A || i16B; println(String + "i16LOr= "  + i16LOr);
    bool i16LXor = i16A ^^ i16B; println(String + "i16LXor= " + i16LXor);
    bool i16LNot = !i16A;        println(String + "i16LNot= " + i16LNot);

    // int32
    int32 i32A = 12;
    int32 i32B = 5;
    int32 i32Add = i32A + i32B;  println(String + "i32Add= " + i32Add);
    int32 i32Sub = i32A - i32B;  println(String + "i32Sub= " + i32Sub);
    int32 i32Mul = i32A * i32B;  println(String + "i32Mul= " + i32Mul);
    int32 i32Div = i32A / i32B;  println(String + "i32Div= " + i32Div);
    int32 i32Mod = i32A % i32B;  println(String + "i32Mod= " + i32Mod);
    int32 i32And = i32A & i32B;  println(String + "i32And= " + i32And);
    int32 i32Or  = i32A | i32B;  println(String + "i32Or= "  + i32Or);
    int32 i32Xor = i32A ^ i32B;  println(String + "i32Xor= " + i32Xor);
    int32 i32Shl = i32A << 1;    println(String + "i32Shl= " + i32Shl);
    int32 i32Shr = i32A >> 1;    println(String + "i32Shr= " + i32Shr);
    int32 i32Pos = +i32A;        println(String + "i32Pos= " + i32Pos);
    int32 i32Neg = -i32A;        println(String + "i32Neg= " + i32Neg);
    int32 i32BNot = ~i32A;       println(String + "i32BNot= " + i32BNot);
    bool i32Eq   = i32A == i32B; println(String + "i32Eq= "  + i32Eq);
    bool i32Ne   = i32A != i32B; println(String + "i32Ne= "  + i32Ne);
    bool i32Lt   = i32A < i32B;  println(String + "i32Lt= "  + i32Lt);
    bool i32Le   = i32A <= i32B; println(String + "i32Le= "  + i32Le);
    bool i32Gt   = i32A > i32B;  println(String + "i32Gt= "  + i32Gt);
    bool i32Ge   = i32A >= i32B; println(String + "i32Ge= "  + i32Ge);
    bool i32LAnd = i32A && i32B; println(String + "i32LAnd= " + i32LAnd);
    bool i32LOr  = i32A || i32B; println(String + "i32LOr= "  + i32LOr);
    bool i32LXor = i32A ^^ i32B; println(String + "i32LXor= " + i32LXor);
    bool i32LNot = !i32A;        println(String + "i32LNot= " + i32LNot);

    // int64
    int64 i64A = 12;
    int64 i64B = 5;
    int64 i64Add = i64A + i64B;  println(String + "i64Add= " + i64Add);
    int64 i64Sub = i64A - i64B;  println(String + "i64Sub= " + i64Sub);
    int64 i64Mul = i64A * i64B;  println(String + "i64Mul= " + i64Mul);
    int64 i64Div = i64A / i64B;  println(String + "i64Div= " + i64Div);
    int64 i64Mod = i64A % i64B;  println(String + "i64Mod= " + i64Mod);
    int64 i64And = i64A & i64B;  println(String + "i64And= " + i64And);
    int64 i64Or  = i64A | i64B;  println(String + "i64Or= "  + i64Or);
    int64 i64Xor = i64A ^ i64B;  println(String + "i64Xor= " + i64Xor);
    int64 i64Shl = i64A << 1;    println(String + "i64Shl= " + i64Shl);
    int64 i64Shr = i64A >> 1;    println(String + "i64Shr= " + i64Shr);
    int64 i64Pos = +i64A;        println(String + "i64Pos= " + i64Pos);
    int64 i64Neg = -i64A;        println(String + "i64Neg= " + i64Neg);
    int64 i64BNot = ~i64A;       println(String + "i64BNot= " + i64BNot);
    bool i64Eq   = i64A == i64B; println(String + "i64Eq= "  + i64Eq);
    bool i64Ne   = i64A != i64B; println(String + "i64Ne= "  + i64Ne);
    bool i64Lt   = i64A < i64B;  println(String + "i64Lt= "  + i64Lt);
    bool i64Le   = i64A <= i64B; println(String + "i64Le= "  + i64Le);
    bool i64Gt   = i64A > i64B;  println(String + "i64Gt= "  + i64Gt);
    bool i64Ge   = i64A >= i64B; println(String + "i64Ge= "  + i64Ge);
    bool i64LAnd = i64A && i64B; println(String + "i64LAnd= " + i64LAnd);
    bool i64LOr  = i64A || i64B; println(String + "i64LOr= "  + i64LOr);
    bool i64LXor = i64A ^^ i64B; println(String + "i64LXor= " + i64LXor);
    bool i64LNot = !i64A;        println(String + "i64LNot= " + i64LNot);

    // uint8 (no unary -)
    uint8 u8A = 12;
    uint8 u8B = 5;
    uint8 u8Add = u8A + u8B;  println(String + "u8Add= " + u8Add);
    uint8 u8Sub = u8A - u8B;  println(String + "u8Sub= " + u8Sub);
    uint8 u8Mul = u8A * u8B;  println(String + "u8Mul= " + u8Mul);
    uint8 u8Div = u8A / u8B;  println(String + "u8Div= " + u8Div);
    uint8 u8Mod = u8A % u8B;  println(String + "u8Mod= " + u8Mod);
    uint8 u8And = u8A & u8B;  println(String + "u8And= " + u8And);
    uint8 u8Or  = u8A | u8B;  println(String + "u8Or= "  + u8Or);
    uint8 u8Xor = u8A ^ u8B;  println(String + "u8Xor= " + u8Xor);
    uint8 u8Shl = u8A << 1;   println(String + "u8Shl= " + u8Shl);
    uint8 u8Shr = u8A >> 1;   println(String + "u8Shr= " + u8Shr);
    uint8 u8Pos = +u8A;       println(String + "u8Pos= " + u8Pos);
    uint8 u8BNot = ~u8A;      println(String + "u8BNot= " + u8BNot);
    bool u8Eq   = u8A == u8B; println(String + "u8Eq= "  + u8Eq);
    bool u8Ne   = u8A != u8B; println(String + "u8Ne= "  + u8Ne);
    bool u8Lt   = u8A < u8B;  println(String + "u8Lt= "  + u8Lt);
    bool u8Le   = u8A <= u8B; println(String + "u8Le= "  + u8Le);
    bool u8Gt   = u8A > u8B;  println(String + "u8Gt= "  + u8Gt);
    bool u8Ge   = u8A >= u8B; println(String + "u8Ge= "  + u8Ge);
    bool u8LAnd = u8A && u8B; println(String + "u8LAnd= " + u8LAnd);
    bool u8LOr  = u8A || u8B; println(String + "u8LOr= "  + u8LOr);
    bool u8LXor = u8A ^^ u8B; println(String + "u8LXor= " + u8LXor);
    bool u8LNot = !u8A;       println(String + "u8LNot= " + u8LNot);
    // unsigned-observable: high-bit operands distinguish udiv/urem/lshr/ugt from signed.
    uint8 u8H = 200;
    uint8 u8uDiv = u8H / 3;   println(String + "u8uDiv= " + u8uDiv);
    uint8 u8uRem = u8H % 3;   println(String + "u8uRem= " + u8uRem);
    uint8 u8uShr = u8H >> 1;  println(String + "u8uShr= " + u8uShr);
    bool  u8uGt  = u8H > u8B; println(String + "u8uGt= "  + u8uGt);

    // uint16
    uint16 u16A = 12;
    uint16 u16B = 5;
    uint16 u16Add = u16A + u16B;  println(String + "u16Add= " + u16Add);
    uint16 u16Sub = u16A - u16B;  println(String + "u16Sub= " + u16Sub);
    uint16 u16Mul = u16A * u16B;  println(String + "u16Mul= " + u16Mul);
    uint16 u16Div = u16A / u16B;  println(String + "u16Div= " + u16Div);
    uint16 u16Mod = u16A % u16B;  println(String + "u16Mod= " + u16Mod);
    uint16 u16And = u16A & u16B;  println(String + "u16And= " + u16And);
    uint16 u16Or  = u16A | u16B;  println(String + "u16Or= "  + u16Or);
    uint16 u16Xor = u16A ^ u16B;  println(String + "u16Xor= " + u16Xor);
    uint16 u16Shl = u16A << 1;    println(String + "u16Shl= " + u16Shl);
    uint16 u16Shr = u16A >> 1;    println(String + "u16Shr= " + u16Shr);
    uint16 u16Pos = +u16A;        println(String + "u16Pos= " + u16Pos);
    uint16 u16BNot = ~u16A;       println(String + "u16BNot= " + u16BNot);
    bool u16Eq   = u16A == u16B; println(String + "u16Eq= "  + u16Eq);
    bool u16Ne   = u16A != u16B; println(String + "u16Ne= "  + u16Ne);
    bool u16Lt   = u16A < u16B;  println(String + "u16Lt= "  + u16Lt);
    bool u16Le   = u16A <= u16B; println(String + "u16Le= "  + u16Le);
    bool u16Gt   = u16A > u16B;  println(String + "u16Gt= "  + u16Gt);
    bool u16Ge   = u16A >= u16B; println(String + "u16Ge= "  + u16Ge);
    bool u16LAnd = u16A && u16B; println(String + "u16LAnd= " + u16LAnd);
    bool u16LOr  = u16A || u16B; println(String + "u16LOr= "  + u16LOr);
    bool u16LXor = u16A ^^ u16B; println(String + "u16LXor= " + u16LXor);
    bool u16LNot = !u16A;        println(String + "u16LNot= " + u16LNot);

    // uint32
    uint32 u32A = 12;
    uint32 u32B = 5;
    uint32 u32Add = u32A + u32B;  println(String + "u32Add= " + u32Add);
    uint32 u32Sub = u32A - u32B;  println(String + "u32Sub= " + u32Sub);
    uint32 u32Mul = u32A * u32B;  println(String + "u32Mul= " + u32Mul);
    uint32 u32Div = u32A / u32B;  println(String + "u32Div= " + u32Div);
    uint32 u32Mod = u32A % u32B;  println(String + "u32Mod= " + u32Mod);
    uint32 u32And = u32A & u32B;  println(String + "u32And= " + u32And);
    uint32 u32Or  = u32A | u32B;  println(String + "u32Or= "  + u32Or);
    uint32 u32Xor = u32A ^ u32B;  println(String + "u32Xor= " + u32Xor);
    uint32 u32Shl = u32A << 1;    println(String + "u32Shl= " + u32Shl);
    uint32 u32Shr = u32A >> 1;    println(String + "u32Shr= " + u32Shr);
    uint32 u32Pos = +u32A;        println(String + "u32Pos= " + u32Pos);
    uint32 u32BNot = ~u32A;       println(String + "u32BNot= " + u32BNot);
    bool u32Eq   = u32A == u32B; println(String + "u32Eq= "  + u32Eq);
    bool u32Ne   = u32A != u32B; println(String + "u32Ne= "  + u32Ne);
    bool u32Lt   = u32A < u32B;  println(String + "u32Lt= "  + u32Lt);
    bool u32Le   = u32A <= u32B; println(String + "u32Le= "  + u32Le);
    bool u32Gt   = u32A > u32B;  println(String + "u32Gt= "  + u32Gt);
    bool u32Ge   = u32A >= u32B; println(String + "u32Ge= "  + u32Ge);
    bool u32LAnd = u32A && u32B; println(String + "u32LAnd= " + u32LAnd);
    bool u32LOr  = u32A || u32B; println(String + "u32LOr= "  + u32LOr);
    bool u32LXor = u32A ^^ u32B; println(String + "u32LXor= " + u32LXor);
    bool u32LNot = !u32A;        println(String + "u32LNot= " + u32LNot);

    // uint64
    uint64 u64A = 12;
    uint64 u64B = 5;
    uint64 u64Add = u64A + u64B;  println(String + "u64Add= " + u64Add);
    uint64 u64Sub = u64A - u64B;  println(String + "u64Sub= " + u64Sub);
    uint64 u64Mul = u64A * u64B;  println(String + "u64Mul= " + u64Mul);
    uint64 u64Div = u64A / u64B;  println(String + "u64Div= " + u64Div);
    uint64 u64Mod = u64A % u64B;  println(String + "u64Mod= " + u64Mod);
    uint64 u64And = u64A & u64B;  println(String + "u64And= " + u64And);
    uint64 u64Or  = u64A | u64B;  println(String + "u64Or= "  + u64Or);
    uint64 u64Xor = u64A ^ u64B;  println(String + "u64Xor= " + u64Xor);
    uint64 u64Shl = u64A << 1;    println(String + "u64Shl= " + u64Shl);
    uint64 u64Shr = u64A >> 1;    println(String + "u64Shr= " + u64Shr);
    uint64 u64Pos = +u64A;        println(String + "u64Pos= " + u64Pos);
    uint64 u64BNot = ~u64A;       println(String + "u64BNot= " + u64BNot);
    bool u64Eq   = u64A == u64B; println(String + "u64Eq= "  + u64Eq);
    bool u64Ne   = u64A != u64B; println(String + "u64Ne= "  + u64Ne);
    bool u64Lt   = u64A < u64B;  println(String + "u64Lt= "  + u64Lt);
    bool u64Le   = u64A <= u64B; println(String + "u64Le= "  + u64Le);
    bool u64Gt   = u64A > u64B;  println(String + "u64Gt= "  + u64Gt);
    bool u64Ge   = u64A >= u64B; println(String + "u64Ge= "  + u64Ge);
    bool u64LAnd = u64A && u64B; println(String + "u64LAnd= " + u64LAnd);
    bool u64LOr  = u64A || u64B; println(String + "u64LOr= "  + u64LOr);
    bool u64LXor = u64A ^^ u64B; println(String + "u64LXor= " + u64LXor);
    bool u64LNot = !u64A;        println(String + "u64LNot= " + u64LNot);

    // uint64 large value — exercises the %llu print fix
    uint64 u64Big = 18_446_744_073_709_551_613;
    println(String + "u64Big= " + u64Big);

    // intptr (typically i64 signed)
    intptr ipA = 12;
    intptr ipB = 5;
    intptr ipAdd = ipA + ipB;  println(String + "ipAdd= " + ipAdd);
    intptr ipSub = ipA - ipB;  println(String + "ipSub= " + ipSub);
    intptr ipMul = ipA * ipB;  println(String + "ipMul= " + ipMul);
    intptr ipDiv = ipA / ipB;  println(String + "ipDiv= " + ipDiv);
    intptr ipMod = ipA % ipB;  println(String + "ipMod= " + ipMod);
    intptr ipAnd = ipA & ipB;  println(String + "ipAnd= " + ipAnd);
    intptr ipOr  = ipA | ipB;  println(String + "ipOr= "  + ipOr);
    intptr ipXor = ipA ^ ipB;  println(String + "ipXor= " + ipXor);
    intptr ipShl = ipA << 1;   println(String + "ipShl= " + ipShl);
    intptr ipShr = ipA >> 1;   println(String + "ipShr= " + ipShr);
    intptr ipPos = +ipA;       println(String + "ipPos= " + ipPos);
    intptr ipNeg = -ipA;       println(String + "ipNeg= " + ipNeg);
    intptr ipBNot = ~ipA;      println(String + "ipBNot= " + ipBNot);
    bool ipEq   = ipA == ipB;  println(String + "ipEq= "  + ipEq);
    bool ipNe   = ipA != ipB;  println(String + "ipNe= "  + ipNe);
    bool ipLt   = ipA < ipB;   println(String + "ipLt= "  + ipLt);
    bool ipLe   = ipA <= ipB;  println(String + "ipLe= "  + ipLe);
    bool ipGt   = ipA > ipB;   println(String + "ipGt= "  + ipGt);
    bool ipGe   = ipA >= ipB;  println(String + "ipGe= "  + ipGe);
    bool ipLAnd = ipA && ipB;  println(String + "ipLAnd= " + ipLAnd);
    bool ipLOr  = ipA || ipB;  println(String + "ipLOr= "  + ipLOr);
    bool ipLXor = ipA ^^ ipB;  println(String + "ipLXor= " + ipLXor);
    bool ipLNot = !ipA;        println(String + "ipLNot= " + ipLNot);

    // float32 — math (incl. %) / comparison / logical / unary (no ~, no bitwise, no shift)
    float32 f32A = 12.5;
    float32 f32B = 2.5;
    float32 f32Add = f32A + f32B;  println(String + "f32Add= " + f32Add);
    float32 f32Sub = f32A - f32B;  println(String + "f32Sub= " + f32Sub);
    float32 f32Mul = f32A * f32B;  println(String + "f32Mul= " + f32Mul);
    float32 f32Div = f32A / f32B;  println(String + "f32Div= " + f32Div);
    float32 f32Mod = f32A % f32B;  println(String + "f32Mod= " + f32Mod);
    float32 f32Pos = +f32A;        println(String + "f32Pos= " + f32Pos);
    float32 f32Neg = -f32A;        println(String + "f32Neg= " + f32Neg);
    bool f32Eq   = f32A == f32B;   println(String + "f32Eq= "  + f32Eq);
    bool f32Ne   = f32A != f32B;   println(String + "f32Ne= "  + f32Ne);
    bool f32Lt   = f32A < f32B;    println(String + "f32Lt= "  + f32Lt);
    bool f32Le   = f32A <= f32B;   println(String + "f32Le= "  + f32Le);
    bool f32Gt   = f32A > f32B;    println(String + "f32Gt= "  + f32Gt);
    bool f32Ge   = f32A >= f32B;   println(String + "f32Ge= "  + f32Ge);
    bool f32LAnd = f32A && f32B;   println(String + "f32LAnd= " + f32LAnd);
    bool f32LOr  = f32A || f32B;   println(String + "f32LOr= "  + f32LOr);
    bool f32LXor = f32A ^^ f32B;   println(String + "f32LXor= " + f32LXor);
    bool f32LNot = !f32A;          println(String + "f32LNot= " + f32LNot);
    // float32 shifts — lhs * (1<<rhs) and lhs / (1<<rhs).
    float32 f32Shl = f32A << 1;    println(String + "f32Shl= " + f32Shl);
    float32 f32Shr = f32A >> 1;    println(String + "f32Shr= " + f32Shr);
    // float32 negatives — bitwise not defined on floating-point.
    // (each reads its local so the type error surfaces ahead of the unused check.)
    //-EXPECT-ERROR: Bitwise '&' not defined on floating-point type 'float32'.
    // float32 f32And = f32A & f32B;
    // println(String + "f32And= " + f32And);
    //-EXPECT-ERROR: Bitwise '|' not defined on floating-point type 'float32'.
    // float32 f32Or  = f32A | f32B;
    // println(String + "f32Or= " + f32Or);
    //-EXPECT-ERROR: Bitwise '^' not defined on floating-point type 'float32'.
    // float32 f32Xor = f32A ^ f32B;
    // println(String + "f32Xor= " + f32Xor);

    // float64
    float64 f64A = 12.5;
    float64 f64B = 2.5;
    float64 f64Add = f64A + f64B;  println(String + "f64Add= " + f64Add);
    float64 f64Sub = f64A - f64B;  println(String + "f64Sub= " + f64Sub);
    float64 f64Mul = f64A * f64B;  println(String + "f64Mul= " + f64Mul);
    float64 f64Div = f64A / f64B;  println(String + "f64Div= " + f64Div);
    float64 f64Mod = f64A % f64B;  println(String + "f64Mod= " + f64Mod);
    float64 f64Pos = +f64A;        println(String + "f64Pos= " + f64Pos);
    float64 f64Neg = -f64A;        println(String + "f64Neg= " + f64Neg);
    bool f64Eq   = f64A == f64B;   println(String + "f64Eq= "  + f64Eq);
    bool f64Ne   = f64A != f64B;   println(String + "f64Ne= "  + f64Ne);
    bool f64Lt   = f64A < f64B;    println(String + "f64Lt= "  + f64Lt);
    bool f64Le   = f64A <= f64B;   println(String + "f64Le= "  + f64Le);
    bool f64Gt   = f64A > f64B;    println(String + "f64Gt= "  + f64Gt);
    bool f64Ge   = f64A >= f64B;   println(String + "f64Ge= "  + f64Ge);
    bool f64LAnd = f64A && f64B;   println(String + "f64LAnd= " + f64LAnd);
    bool f64LOr  = f64A || f64B;   println(String + "f64LOr= "  + f64LOr);
    bool f64LXor = f64A ^^ f64B;   println(String + "f64LXor= " + f64LXor);
    bool f64LNot = !f64A;          println(String + "f64LNot= " + f64LNot);
    // float64 shifts — lhs * (1<<rhs) and lhs / (1<<rhs).
    float64 f64Shl = f64A << 1;    println(String + "f64Shl= " + f64Shl);
    float64 f64Shr = f64A >> 1;    println(String + "f64Shr= " + f64Shr);
    // float64 negatives — bitwise not defined on floating-point.
    // (each reads its local so the type error surfaces ahead of the unused check.)
    //-EXPECT-ERROR: Bitwise '&' not defined on floating-point type 'float64'.
    // float64 f64And = f64A & f64B;
    // println(String + "f64And= " + f64And);
    //-EXPECT-ERROR: Bitwise '|' not defined on floating-point type 'float64'.
    // float64 f64Or  = f64A | f64B;
    // println(String + "f64Or= " + f64Or);
    //-EXPECT-ERROR: Bitwise '^' not defined on floating-point type 'float64'.
    // float64 f64Xor = f64A ^ f64B;
    // println(String + "f64Xor= " + f64Xor);

    /* --- Type(value): non-int primitive temporaries, bound by the DECL-INIT rules --- */
    println(String + "tf32= "  + float32(1.5));
    println(String + "tf64= "  + float64(2.5));
    println(String + "tflt= "  + float(3.5));
    println(String + "tbool= " + bool(true));
    println(String + "tchar= " + char('A'));
    float32 twf = 1.25;
    println(String + "twiden= " + float64(twf));   // float32 source widens into float64

    /* --- Type(value) negatives: one //-block uncommented per run --- */

    //-EXPECT-ERROR: Cannot implicitly convert 'int' to 'float64'
    // float64 tcf = float64(5);
    // println(String + "tcf= " + tcf);

    //-EXPECT-ERROR: Cannot implicitly narrow 'float64' to 'float32'
    // float64 twd = 1.5;
    // float32 tnf = float32(twd);
    // println(String + "tnf= " + tnf);

    //-EXPECT-ERROR: A primitive temporary 'Type(value)' requires exactly one value.
    // float64 tz = float64();
    // println(String + "tz= " + tz);

    return 0;
}
