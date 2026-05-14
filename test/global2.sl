/*
phase-6 reach-goal negative tests.

each //-block below is a complete tiny program. the negative-test runner
uncomments one block at a time, runs slidsc on the result, and asserts
the marker substring appears in stderr. this file has no positive content
on its own — when the runner is not active it does not compile, by design.
*/

int leader_ = 0;

int32 main() {

    /* double `global;` in main: spec allows at most one per program. */
    //-EXPECT-ERROR: cannot appear more than once
    //global;
    //global;

    /* access in main before `global;` opens its scope. user wrote `global;` */
    /* in a nested block, so auto-insert is suppressed and the top of main is */
    /* outside the lifetime. */
    //-EXPECT-ERROR: outside the `global;` scope
    //int v = ::leader_;
    //{
    //    global;
    //}

    return 0;
}
