/*
import declarations from a header file.
use the declared functions.
link with the compiled implementation.

multi-TU coverage:
- main's `global;` (auto-inserted) closes the lifetime scope on return
- the lazy slid `state` lives in the other TU; its dtor must still fire
- reverse construction order is testable when more lazies join later
*/

import import_decl;

int32 main() {
    printHelloWorld();
    printHelloWorld();
    printGoodbye();
    return 0;
}
