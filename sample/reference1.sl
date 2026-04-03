int32 main() {
    int32 x = 0;
    int32^ ref = ^x;
    ref^ = ref^ + 1;
    ref^ += 1;
    ref^++;
    ++(ref^);
    println("expected: 4");
    println(ref^);
    return 0;
}
