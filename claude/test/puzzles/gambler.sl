/*
gambler simulation

*/

import string;


uint32 g_next = 1;

uint32 myrand() {
    g_next = g_next * 1103515245 + 12345;
    return (g_next/65536) % 32768;
}

void mysrand(uint32 seed) {
    g_next = seed;
}

enum ( kBlack, kWhite );

int32 main() {

    println(String + "Gambler:");
    mysrand(97531);

    ntrials = 0;
    nblacks = 0;

    for (i : 0..1_000_000) {
        r = myrand() % 3;
        up = kBlack;
        dn = kWhite;
        switch (r) {
            0: {
                up = kBlack;
                dn = kBlack;
            }
            1: {
                up = kWhite;
                dn = kWhite;
            }
            default: {
                r = myrand() % 2;
                if (r == 0) {
                    up <--> dn;
                }
            }
        }
        if (up == kBlack) {
            ++ntrials;
            if (dn == kBlack) {
                ++nblacks;
            }
        }
    }

    prob = (float=nblacks) / (float=ntrials);
    println(String + "ntrials=" + ntrials + " nblacks=" + nblacks + " prob=" + prob);

    return 0;
}
