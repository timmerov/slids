/*
pointers to string literals.
*/

int32 main() {

    /* this should be a compile error: */
    char[] mutable_ptr = "immutable string literal.";

    /* these are valid. */
    (const char)[] good_ptr = nullptr;
    good_ptr = "hello";
    good_ptr = "there";
    good_ptr = nullptr;

    return 0;
}
