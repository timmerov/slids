
int32 main() {
    int32 x = 6;
    int32 y = 7;
    int32 z = x * y;
    __println(z);
    x *= y;
    __println(x);

    /* auto-widen: a typed value flows into a wider type. */
    intptr a = x;
    __println(a);
    int64 b = y;
    __println(b);

    /* integer literals have no fixed type — they flex to a narrower target. */
    char ch = 65;
    __println(ch);
    int8 c = 100;
    __println(c);
    int16 d = 30000;
    __println(d);
    int8 e = -5;
    __println(e);

    /* a decimal literal with no declared type infers int. */
    q = 7;
    __println(q);

    /* widening a typed narrow value into a wide one. */
    int64 w = c;
    __println(w);

    return 0;
}
