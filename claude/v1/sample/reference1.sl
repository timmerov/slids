int32 main() {
    int32 x = 0;
    int32^ ref = ^x;
    ref^ = ref^ + 1;
    ref^ += 1;
    ref^++;
    ++(ref^);
    __println("expected: 4");
    __println(ref^);
    return 0;
}
