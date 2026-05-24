/*
test malformed comments.
each //-EXPECT-ERROR case is exercised by run_negatives.sh.
the file itself lexes clean — all error cases are //-prefixed and
get uncommented one at a time by the runner.
*/

//-EXPECT-ERROR: unmatched [*][/]
// [*][/]

//-EXPECT-ERROR: unterminated block comment
// [/][*]

//-EXPECT-ERROR: whitespace between line-continuation \ and newline
//// trailing [\\][   ]
//   continuation

//-EXPECT-ERROR: escaped newline breaking comment token [/][/]
// [/][\\][\n][/]

//-EXPECT-ERROR: escaped newline breaking comment token [/][*]
// [/][\\][\n][*]

//-EXPECT-ERROR: escaped newline breaking comment token [*][/]
// [*][\\][\n][/]

int32 main() {
    return 0;
}
