/*
test private fields in classes.
importa a class with private fields.
we don't know the sizeof the class from the header.
we need to get it from elsewhere.
*/

import opaque;

int32 main() {
    Opaque dark;
    dark.printSecretMessage();
    return 0;
}
