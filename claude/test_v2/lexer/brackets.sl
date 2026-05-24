/*
test unbalanced brackets.

each //-EXPECT-ERROR case is exercised by run_negatives.sh.
the file itself lexes clean — all error cases are //-prefixed and
get uncommented one at a time by the runner.
*/

//-EXPECT-ERROR: mismatched bracket: expected ')', got '}'
// ( }

//-EXPECT-ERROR: mismatched bracket: expected ')', got ']'
// ( ]

//-EXPECT-ERROR: mismatched bracket: expected '}', got ')'
// { )

//-EXPECT-ERROR: mismatched bracket: expected '}', got ']'
// { ]

//-EXPECT-ERROR: mismatched bracket: expected ']', got ')'
// [ )

//-EXPECT-ERROR: mismatched bracket: expected ']', got '}'
// [ }

//-EXPECT-ERROR: unmatched ')'
// )

//-EXPECT-ERROR: unmatched '}'
// }

//-EXPECT-ERROR: unmatched ']'
// ]

//-EXPECT-ERROR: unterminated '('
// (

//-EXPECT-ERROR: unterminated '{'
// {

//-EXPECT-ERROR: unterminated '['
// [

int32 main() {
    return 0;
}
