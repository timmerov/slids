/*
test claims about blocks.
*/

/*
learned:

bare blocks cannot have names.
update docs or implement.

labels need to end with semicolon.
fix.

for loops do not have the default name.
for loops do not have the default name.
*/

in32 main() {
    __println("start main.");

    for int x in (0..1) {
        __println("start block.");

        /* goto end of block. */
        switch (3) {
        case 1:
            break block;
        case 2:
            break;
        }

        __println("end block.");
    } :block;

    __println("end main.");

    return 0;
}
