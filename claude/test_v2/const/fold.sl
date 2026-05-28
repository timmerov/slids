/*
test constant folding.

repeatedly searches the parse tree for:
unary operations on a numeric literal,
binary operations where both are literals.
these are replaced with the result of the operation.

widen rules apply.
compile error when widen rules are violated.
*/

int32 main() {
    return 0;
}
