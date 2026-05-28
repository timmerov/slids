/*
ensure the lexer rejects invalid characters.
*/

//-EXPECT-ERROR: unexpected character: '@'
// @

//-EXPECT-ERROR: unexpected character: '$'
// $

//-EXPECT-ERROR: unexpected character: '`'
// `

//-EXPECT-ERROR: Unterminated character literal.
// char unterm_ch = 'abcdef

//-EXPECT-ERROR: Unterminated string literal.
// char[] unterm_str = "Hello, World!

int32 main() {

    /* valid string escapes. */
    char[] str;
    str = "Hello \0\t\n\\\" World!";

    //-EXPECT-ERROR: unknown escape sequence: '\q'
    // str = "Hello [\\]q World!";

    __println("Hello, World!");

    return 0;
}
