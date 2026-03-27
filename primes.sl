import std.io;
import std.vector;

int32 main() {
    Vector primes;

    for int n in (2..1000) {
        bool is_prime = true;

        for int p in primes {
            if (p * p > n) {
                break;
            }
            if (n % p == 0) {
                is_prime = false;
                break;
            }
        }

        if (is_prime) {
            primes.add(n);
            println(n);
        }
    }
    return 0;
}
