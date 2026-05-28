/*
test constant folding.

constant folding determines the nominal size of all literals.
unary and binary operations on literals are replaced
with the result.
which has its own nominal size.

constant literals have a lexical kind.
this is to be preserved as much as possible.
kinds: bool, char, integer, unsigned, float.

char type is platform dependent - uint8 is assumed.
char kind should be preserved as much as possible.

constants have a nominal type determined by their value.
number of bits required, then signed/unsigned.
bool     -> uint1
char     -> uint8
integer  -> int 8,16,32,64
         -> uint 1,8,16,32,64
unsigned -> uint 1,8,16,32,64
float    -> float 32,64

constants have a default computation type.
bool     -> uint64
char     -> int64
integer  -> int64
unsigned -> uint64
float    -> float64
those are the three computation types.

the computation type that is compatible with two constants depends on their
kinds and values.
if both kinds are float use float64,
else if one kind is float then compile error,
else if both kinds are unsigned use uint64,
else if the nominal size of one constant is uint64
    if the value of the other constant is negative then compile error
    else use uint64,
else use int64.

constants are stored as strings.
the kind field determines how the numeric value is converted to a string.
and how the string is converted to a numeric value.
these are the c++ calls to use.
kind                    to string               from string
bool, char, integer     to_string(int64(v))     strtoll
unsigned                to_string(uint64(v))    strtoull
float                   snprintf("%.17g")       strtod
the type of the numeric value is the computation type.
"widening" is performed by truncating the numeric value from the computation type
directly to the target type.

general catch-all rule:
it is a compile error if any widening rule fails for any reason.
cannot be applied.
cannot be found.

literals with nominal type float32 may be silently widened to float64.
literals with nominal type integer-class (bool, char, intN, uintN) may be silently
widened to any larger integer-class types.
for examples:
33_000 may be any of these uint16, uint32, uint64, int32, int64.
-27 may be int8, int16, int32, int64.
floating point literals are silently rounded to match the target type precision.
3.14 becomes 3.1400001049 for target float32.
floating point types and integer-class types never silently mix -
an explicit type conversion is required.

the nominal type of the result of an operation is determined by its value.
exceptions are noted.
the kind of the result of an operation is determined by the operation.
the string encoding of the result is determined by kind.

it is possible for the value of a constant to not fit into its nominal type.
this is valid and unambiguous.
these cases will be handled downstream.

invalid math operations are compile errors.
for examples:
divide by zero,
shift by negative value,
shift by non-integer value,
any floating point error,
integer over/under-flow,
etc.

these widening rules apply to operations on numeric literals:

1. unary operations: + - ~ !

1a. unary + is a nop.
the result is the kind of the operand.

1b. unary !.
no widening - all literal types are accepted.
the result is bool.

1c. unary - on float.
the operand is widened to float64.
the result is float.

1d. unary - on bool, char, integer, unsigned.
the operand is always widened to int64.
the result is integer.

1e. unary ~ on bool, char, integer, unsigned.
the operand is widened to its computation type.
the kind is preserved except bool becomes unsigned.
the nominal size of the result is the nominal size of the operand.

1f. unary ~ on float.
compile error.

2. comparison operations: == != <= >= < >
the nominal types of the operands are widened to a compatible computation type:
int64, uint64, float64.
the result is bool.

3. logical operations: && || ^^
no widening - all types accepted.
logical operations consume constant-expression.
0-like values are false.
everything else is true.
the result is bool.

4. shift operations: << >>
the lhs is widened to its computation type.
if the lhs is float then the operation is mathematically equivalent to:
lhs * (1<<rhs) and lhs / (1<<rhs).
the kind of the result is the kind of the lhs.

5. bitwise operations: & | ^

5a. bitwise operations on floats
compile error.

5b. bitwise operations on bool, char, integer, unsigned.
the nominal types of the operands are widened to a compatible computation type:
int64, uint64
if both operands are the same kind, the result is that kind,
otherwise the kind is integer.

6. math operations: + - * / %
the nominal types of the operands are widened to a compatible computation type:
int64, uint64, float64.
the computation type determines the kind.
exception: if the result overflows int64 but not uint64 then the kind is unsigned.
*/

int32 main() {
    return 0;
}
