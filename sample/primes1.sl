
int32 main() {
    int32 count = 0;
    for int n in (2..1000) {
        bool is_prime = 1;
        for int i in (2..n) {
            if (i * i > n) {
                break;
            }
            if (n % i == 0) {
                is_prime = 0;
                break;
            }
        }
        if (is_prime) {
            print(n);
            count = count + 1;
            if (count % 10 == 0) {
                println("");
            } else {
                print(" ");
            }
        }
    }
    println("");
    return 0;
}
