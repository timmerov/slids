/*
error message wish list.

die at first error.
no warnings.
it's either allowed or it's not.
*/

int32 main() {
    _println("Hello, World!");
    return 0;
}

/*
that doesn't compile.
println is missing an underscore.
dump this error message:

error_messages.sl:
_8:
_9:int32 main() {
10:    _println("Hello, World!");
       ^------^
11:    return 0;
12:}
unexpected identifier '_println'.

more generally...

print the file stack:
error_messages.sl: imported
string.sl: imported
dump.sl:

then print 3 lines ending with the offending line.
then print the indicator cart under the offending token.
^ or ^^ or ^------^
then print 2 more lines.
then deliver the error message.

so this means we need:
a file stack.
probably global scope.
the line number, start column, length.
if it were me, the lexer would store a big table
of token information per file.
the parse would track tokens by index into this table.
i'd avoid passing token number to everything.
just keep it updated somewhere convenient.
*/
