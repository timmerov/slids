/*
test the built-in primitives.

number widening rules:
these rules apply everywhere widening can happen.
including but not limited to: expressions, literals, parameters, variables, etc.

signed integers are silently widened to larger signed integers.
unsigned integers are silently widened to larger unsigned integers.
unsigned integers are silently widened to a larger unsigned integer,
then silently reinterpreted as a signed integer of the same larger size.
floating point numbers are silently widened to larger floating point numbers.

truncating to a smaller number type without explicit type conversion is a compile error.
converting signed integers to unsigned without explicit type .conversion is a compile error.

integer literals are flexible type.
their default type is int,
unless the value is too big,
then the type is int64,
unless the value is too big,
then the type is uint64.

bool is a 1 bit unsigned integer.

char, intptr, float, are types that depends on the compiler, the os, or the weather.
char is typically uint8.
intptr is typically int64.
float is typically float32.
maybe these are configurable in the compiler.
maybe not.
the author should not make assumptions.

bool, char, intptr, float follow the rules that apply to their implementation type.
*/

int32 main() {

    bool xb = true;
    char xc = 'A';
    int xi = 1;
    uint xu = 2;
    intptr xip = 3;
    int8 xi8 = 4;
    int16 xi16 = 5;
    int32 xi32 = 6;
    int64 xi64 = 7;
    uint8 xu8 = 8;
    uint16 xu16 = 9;
    uint32 xu32 = 10;
    uint64 xu64 = 11;
    float xf = 12.0;
    float32 xf32 = 13.0;
    float64 xf64 = 14.0;

    xb = 0;
    xb = 1;
    //-EXPECT-ERROR: Integer literal -1 does not fit in 'bool'.
    // xb = -1;
    //-EXPECT-ERROR: Integer literal 2 does not fit in 'bool'.
    // xb = 2;

    xi8 = 127;
    xi8 = -128;
    //-EXPECT-ERROR: Integer literal 128 does not fit in 'int8'.
    // xi8 = 128;
    //-EXPECT-ERROR: Integer literal -129 does not fit in 'int8'.
    // xi8 = -129;

    xi16 = 32_767;
    xi16 = -32_768;
    //-EXPECT-ERROR: Integer literal 32768 does not fit in 'int16'.
    // xi16 = 32_768;
    //-EXPECT-ERROR: Integer literal -32769 does not fit in 'int16'.
    // xi16 = -32_769;

    xi32 = 2_147_483_647;
    xi32 = -2_147_483_648;
    //-EXPECT-ERROR: Integer literal 2147483648 does not fit in 'int32'.
    // xi32 = 2_147_483_648;
    //-EXPECT-ERROR: Integer literal -2147483649 does not fit in 'int32'.
    // xi32 = -2_147_483_649;

    xi64 = 9_223_372_036_854_775_807;
    xi64 = -9_223_372_036_854_775_808;
    //-EXPECT-ERROR: Integer literal 9223372036854775808 does not fit in 'int64'.
    // xi64 = 9_223_372_036_854_775_808;
    //-EXPECT-ERROR: Integer literal -9223372036854775809 does not fit in 'int64'.
    // xi64 = -9_223_372_036_854_775_809;

    xu8 = 0;
    xu8 = 255;
    //-EXPECT-ERROR: Integer literal -1 does not fit in 'uint8'.
    // xu8 = -1;
    //-EXPECT-ERROR: Integer literal 256 does not fit in 'uint8'.
    // xu8 = 256;

    xu16 = 0;
    xu16 = 65_535;
    //-EXPECT-ERROR: Integer literal -1 does not fit in 'uint16'.
    // xu16 = -1;
    //-EXPECT-ERROR: Integer literal 65536 does not fit in 'uint16'.
    // xu16 = 65_536;

    xu32 = 0;
    xu32 = 4_294_967_295;
    //-EXPECT-ERROR: Integer literal -1 does not fit in 'uint32'.
    // xu32 = -1;
    //-EXPECT-ERROR: Integer literal 4294967296 does not fit in 'uint32'.
    // xu32 = 4_294_967_296;

    xu64 = 0;
    xu64 = 18_446_744_073_709_551_615;
    //-EXPECT-ERROR: Integer literal -1 does not fit in 'uint64'.
    // xu64 = -1;
    //-EXPECT-ERROR: Integer literal 18446744073709551616 does not fit in 'uint64'.
    // xu64 = 18_446_744_073_709_551_616;

    xu = 0;
    xu = 4_294_967_295;
    //-EXPECT-ERROR: Integer literal -1 does not fit in 'uint'.
    // xu = -1;
    //-EXPECT-ERROR: Integer literal 4294967296 does not fit in 'uint'.
    // xu = 4_294_967_296;

    xf32 = 1.0;
    xf32 = -1.0;
    //-EXPECT-ERROR: Float literal 1e40 does not fit in 'float32'.
    // xf32 = 1e40;
    //-EXPECT-ERROR: Float literal -1e40 does not fit in 'float32'.
    // xf32 = -1e40;

    __println("Hello, World!");

    return 0;
}
