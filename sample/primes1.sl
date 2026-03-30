/*
Copyright Tim Cotter 2026. All rights reserved.
*/

/*
find all primes less than 1000.
print 10 primes per line.
*/

int32 main() {
    println("all primes less than 1000:");
    int32 count = 0;
    for int n in (2..1000) {
        bool is_prime = 1;
        for int i in (2..n) {
            /* this test failes when i >= 2^7 * sqrt(2) */
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

            /** space or newline **/
            count = count + 1;
            if (count % 10 == 0) {
                println();
            } else {
                print(" ");
            }
        }
    }
    if (count % 10) {
        println();
    }
    return 0;
}
