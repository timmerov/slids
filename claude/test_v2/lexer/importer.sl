/*
test the lexer's ability to import a file.
*/

import importee1;
import importee2;

//-EXPECT-ERROR: cannot find 'file_does_not_exist.slh' on the import path
//import file_does_not_exist;

int32 main() {
    return 0;
}
