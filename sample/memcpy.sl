
int32 main() {

    char text[14] = ('H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!', '\0');
    char dest[14];
    char[] from = ^text[0];
    char[] to = ^dest[0];
    char ch = 0;
    while {
        ch = from++^;
        to++^ = ch;
    } (ch != 0);

    __println("expected: Hello, World!");
    __println(dest);

    return 0;
}
