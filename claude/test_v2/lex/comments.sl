/*
test malformed comments.

each //-EXPECT-ERROR case is exercised by run_negatives.sh.
the file itself lexes clean — all error cases are //-prefixed and
get uncommented one at a time by the runner.
*/

//-EXPECT-ERROR: Unmatched '[*][/]'
// [*][/]

//-EXPECT-ERROR: Unterminated block comment
// [/][*]

//-EXPECT-ERROR: Whitespace between line-continuation '\' and newline
//// trailing [\\][   ]
//   continuation

//-EXPECT-ERROR: Escaped newline breaking comment token '[/][/]'
// [/][\\][\n][/]

//-EXPECT-ERROR: Escaped newline breaking comment token '[/][*]'
// [/][\\][\n][*]

//-EXPECT-ERROR: Escaped newline breaking comment token '[*][/]'
// [*][\\][\n][/]

int32 main() {
    __println("Hello, World!");
    return 0;
}
