/*
test long form for loop.

long for:

for (varlist) (cond) {update} {body}

varlist is a comma separated tuple of variable declarations.
type is infered (when that lands).
initializers are technically optional.
the variables in the var list are allocated once, outside the
update and body code blocks.

and empty condition clause is always true.

the update code block may not continue, break, or return.

the loop body is executed as long as the condition is true.

variables are re-used from an enclosing scope when possible.

    int x = -1;
    for (x = 0) (x < 10) { ++x; } {
        __println(x);
    }
    __println("x should be 10: " + x);
*/

int32 main() {

    return 0;
}
