/*
ensure the lexer rejects invalid characters.
*/

//-EXPECT-ERROR: unexpected character: '@'
// @

//-EXPECT-ERROR: unexpected character: '$'
// $

//-EXPECT-ERROR: unexpected character: '`'
// `

int32 main() {

    /* valid char escapes. */
    char ch;
    ch = '\0';
    ch = '\t';
    ch = '\n';
    ch = '\\';
    ch = '\'';

    //-EXPECT-ERROR: unknown escape sequence: '\q'
    // ch = '[\\]q'

    /* valid string escapes. */
    char[] str;
    str = "Hello \0\t\n\\\" World!";

    //-EXPECT-ERROR: unknown escape sequence: '\q'
    // str = "Hello [\\]q World!";

    __println("Hello, World!");

    return 0;
}
