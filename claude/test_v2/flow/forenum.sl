/*
test for over enum.

    for (var : enum) {body}

var and enum are required.
body may be empty.
parentheses and curly brackets are required.

desugars to:

    for (
        type var = enum.first,
    ) (
        var <= enum.last
    ) {
        var++;
    } {
        body
    }

note:
the range is over the *first* enum defined
to the *last* enum defined.
not the minimum and maximum values of the enum.
if the enum is not contiguous, then the behavior is defined
but should be used with caution.

note:
should we check for contiguousness?
lean no.
*/

int32 main() {

    return 0;
}
