/*
slid field default bug.

code fails to repro the bug.
might have surfaced in the middle of a landing.
never to be seen again.

the expected result is bad ll.
*/

Simple(int x_ = 0) { }
Container(int y_ = 0, Simple s_) { }


int32 main() {
    Container c;
    return 0;
}
