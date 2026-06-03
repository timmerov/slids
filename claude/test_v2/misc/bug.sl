/*
resolve the bug of the day.
*/

enum char Demo (
    a = 'a',
    b,
    c
);

int32 main() {

    __println(##type(Demo:a) + " Demo:a = " + Demo:a);
    __println(##type(Demo:b) + " Demo:b = " + Demo:b);
    __println(##type(Demo:c) + " Demo:c = " + Demo:c);

    a = Demo:a;
    b = Demo:b;
    c = Demo:c;

    __println(##type(a) + " a = " + a);
    __println(##type(b) + " b = " + b);
    __println(##type(c) + " c = " + c);

    return 0;
}
