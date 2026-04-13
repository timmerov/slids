/*
test private fields in classes.

import a class with private fields.
get the sizeof from the annotation.
if it's not there or is delete, then there's a build error.
this file must be compiled after opaque.sl.
*/

/* imports the annotated version build/opaque.slh */
import opaque;

int32 main() {
    /* alloca the annotated sizeof Opaque, not the size of the public variables. */
    Opaque dark;
    dark.printSecretMessage();
    return 0;
}
