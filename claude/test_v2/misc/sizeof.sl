/*
test sizeof built-in function.

sizeof returns the size in bytes of the type or expression.
the return type is intptr.
the sizeof a string literal includes the terminating null.

    sizeof(Type)
    sizeof(string-literal)
    sizeof(expression)

examples:

    Type x;
    Type^ ref;
    Type arr[N];

    sizeof(Type);
    sizeof(x);
    sizeof(ref);
    sizeof(ref^);
    sizeof(arr);
    sizeof("Hello, World!");  // 14

notes:
sizeof is usually a foldable constant expression.
sizeof accepts classes when they land.
sizeof accepts qualified types.
*/

/*
claude says:

tbd
*/

int32 main() {

    return 0;
}
