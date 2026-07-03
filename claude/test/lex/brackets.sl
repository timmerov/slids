/*
test unbalanced brackets.

each //-EXPECT-ERROR case is exercised by run_negatives.sh.
the file itself lexes clean — all error cases are //-prefixed and
get uncommented one at a time by the runner.
*/

//-EXPECT-ERROR: Mismatched bracket: expected ')', got '}'
// ( }

//-EXPECT-ERROR: Mismatched bracket: expected ')', got ']'
// ( ]

//-EXPECT-ERROR: Mismatched bracket: expected '}', got ')'
// { )

//-EXPECT-ERROR: Mismatched bracket: expected '}', got ']'
// { ]

//-EXPECT-ERROR: Mismatched bracket: expected ']', got ')'
// [ )

//-EXPECT-ERROR: Mismatched bracket: expected ']', got '}'
// [ }

//-EXPECT-ERROR: Unmatched ')'
// )

//-EXPECT-ERROR: Unmatched '}'
// }

//-EXPECT-ERROR: Unmatched ']'
// ]

//-EXPECT-ERROR: Unterminated '('
// (

//-EXPECT-ERROR: Unterminated '{'
// {

//-EXPECT-ERROR: Unterminated '['
// [

int32 main() {
    __println("Hello, World!");
    return 0;
}
