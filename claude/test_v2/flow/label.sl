/*
test lables on for/while loops.
include numbered and named breaks and continues.
includes breaks and continues in switch statements.

the label goes after the closing curly bracket.
followed by semi-colon if necessary.

    while (cond) {
        while {
            if (cond) {
                /* break from the inner loop. */
                break;
            switch (value) {
            case 0:
                /* break from the switch. */
                break;
            case 1:
                /* break from the inner loop. */
                break 1;
            case 2:
                /* break from the inner loop. */
                break inner;
            case 3:
                /* continue the inner loop. */
                continue;
            case 4:
                /* continue the inner loop. */
                continue 1;
            case 5:
                /* continue the inner loop. */
                continue inner;
            case 6:
                /* break from the outer loop. */
                break 2;
            case 7:
                /* break from the outer loop. */
                break outer;
            case 8:
                /* continue the outer loop. */
                continue 2;
            case 9:
                /* continue the outer loop. */
                continue outer;
            }
        } :inner (cond);
    } :outer;

the default name of a for loop is for.
the default name of a while loop is while.

*/

/*
claude says:

tbd.
*/

int32 main() {

    return 0;
}
