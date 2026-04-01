/*
copyright tim cotter 2026. all rights reserved.
*/

/*
print numbers from 1 to 100.
10 numbers per line.
if the number is divisible by 3 print biz instead.
if the number is divisible by 5 print buzz instead.
if the number is divisible by both print biz-buzz.
*/

int32 main() {
    for int i in (1..100+1) {
        bool by3 = (i % 3 == 0);
        bool by5 = (i % 5 == 0);
        if (by3 && by5) {
            print("biz-buzz");
        } else if (by3) {
            print("biz");
        } else if (by5) {
            print("buzz");
        } else {
            print(i);
        }

        if (i % 10) {
            print(" ");
        } else {
            println();
        }
    }
    println();
    return 0;
}
