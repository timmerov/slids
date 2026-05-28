/*
test constant folding.

repeatedly searches the parse tree for:
unary operations on a numeric literal,
binary operations where both are literals.

widen rules apply.
compile error when widen rules are violated.

every literal is given a nominal type based on its value:
bool, char, intN, uintN, floatN.
*/

int32 main() {
    return 0;
}
