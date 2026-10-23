/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
test the lexer's ability to import a file.
*/

import string;
alias slids;

import importee1;
import importee2;

//-EXPECT-ERROR: Cannot find 'file_does_not_exist.slh' on the import path
//import file_does_not_exist;

int32 main() {
    println(String + "Hello, World!");
    return 0;
}
