/*
nested comments.

note:
it's difficult to make a compile-able file
where the comments are describing comment tokens.
whereever possible, the actual tokens are used:
// /* */
where not possible underscores are artificially
inserted. pretend they're not there.
analogous to parsing numbers.
interpret as same symbols above:
/_/ /_* *_/

the rules:

single line comments //
end at the first non-escaped new line.

block comments /* */
begin at the /* and end at the balancing */

single and block comments may be interleaved.
a block comment starts at the /* and ends at
the corresponding */ even when they are
inside a line comment.

a line comment continues to the end of line
even if the line contains a *_/ that closes
a block comment.

the // /* */ tokens are consumed as units.
notable cases:
//_*
    a single line comment.
    it does not start a block comment.
*_//
    ends a block comment.
    there is a trailing / for the lexer/parser.
    it does not start a single line comment.

a character is inside a comment if:
it is between // and the next unescaped newline or
it is between nested /* and */ tokens.

unbalanced /* and */ tokens is an error.
*/

int32 main() {
    /*
    this is a simple block comment.
    */
    // single line comment.
    // "single" line comment \
        that continues on the next lines \
        because the lines end with backslash

    /*
    this is a compile error because there
    is only whitespace between the backslash
    and the end of line.

    this test needs special handling because
    codeblocks strips the trailing whitespace.
    */
    // compile error \

    /*
    this is a compile error.
    you may not split a block token with an
    escaped newline.
    do not do this.
    i will burn your house.
    seriously.
    do. not. do. this.
    */
    /*
/\
*
*\
/
    */

    /*
        /*
        nested block comment.
        */
        // nested single line comment.
        // nested "single" line comment \
            with continuation.
    */

    // hybridc case /* line and block comment
    this is still inside the block. */

    /*
    this is a weird hybrid case.
    // comment */ still comment

    __println("Hello, nested and doubly commented World!");

    return 0;
}
