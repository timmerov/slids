import std.io;
import std.vector;

int32 main() {
    Vector primes;

    for int n in (2..1000) {

        bool isPrime() {
            for int p in primes {
                if (p * p > n) {
                    return true;
                }
                if (n % p == 0) {
                    return false;
                }
            }
        }

        if (isPrime()) {
            primes.add(n);
            __println(n);
        }
    }

    return 0;
}
