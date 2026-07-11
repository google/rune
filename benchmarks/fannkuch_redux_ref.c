// C reference implementation of fannkuch_redux benchmark.
// Matches output of benchmarks/fannkuch_redux.rn byte-for-byte.
//
// Algorithm: direct port of the Rune 1-indexed Knuth-Schrage-Rosenkrantz variant.
// Output: two lines — signed 64-bit checksum, then "Pfannkuchen(N) = maxflips".

#include <stdio.h>
#include <stdlib.h>

enum { MAX_N = 16 };

typedef struct { long long sum; unsigned maxflips; } FannResult;

static FannResult fannkuch(unsigned n) {
    if (n < 1 || n > MAX_N) {
        fprintf(stderr, "fannkuch n must be in 1..16\n");
        exit(EXIT_FAILURE);
    }
    if (n == 1) return (FannResult){0, 0};
    if (n == 2) return (FannResult){-1, 1};

    // 1-indexed arrays of size n+1 (index 0 unused)
    unsigned *p = (unsigned *)calloc(n + 1, sizeof(unsigned));
    unsigned *q = (unsigned *)calloc(n + 1, sizeof(unsigned));
    unsigned *s = (unsigned *)calloc(n + 1, sizeof(unsigned));

    for (unsigned i = 1; i <= n; i++) {
        p[i] = q[i] = s[i] = i;
    }

    long long sign = 1, sum = 0;
    unsigned maxflips = 0;

    while (1) {
        unsigned q0 = p[1];
        if (q0 != 1) {
            // Copy p[2..n] into q[2..n] (q[1] intentionally not copied)
            for (unsigned i = 2; i <= n; i++) q[i] = p[i];
            unsigned flips = 1;
            // Count flips: do { qq = q[q0] } while qq != 1 { ... }
            unsigned qq = q[q0];
            while (qq != 1) {
                q[q0] = q0;
                if (q0 >= 4) {
                    unsigned i = 2, j = q0 - 1;
                    // do { swap } while i < j
                    do {
                        unsigned t = q[i]; q[i] = q[j]; q[j] = t;
                        i++; j--;
                    } while (i < j);
                }
                q0 = qq;
                flips++;
                qq = q[q0];
            }
            sum += sign * (long long)flips;
            if (flips > maxflips) maxflips = flips;
        }

        // Generate next permutation (sign-alternating Ehrlich scheme)
        if (sign == 1) {
            unsigned t = p[2]; p[2] = p[1]; p[1] = t;
            sign = -1;
        } else {
            unsigned t = p[2]; p[2] = p[3]; p[3] = t;
            sign = 1;
            int cont = 1;
            for (unsigned i = 3; i <= n && cont; i++) {
                unsigned sx = s[i];
                if (sx != 1) {
                    s[i] = sx - 1;
                    cont = 0;
                } else {
                    if (i == n) {
                        FannResult r = {sum, maxflips};
                        free(p); free(q); free(s);
                        return r;
                    }
                    s[i] = i;
                    unsigned t2 = p[1];
                    for (unsigned j = 1; j <= i; j++) p[j] = p[j + 1];
                    p[i + 1] = t2;
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    unsigned n = 12;
    if (argc > 1) n = (unsigned)atoi(argv[1]);
    FannResult r = fannkuch(n);
    printf("%lld\n", r.sum);
    printf("Pfannkuchen(%u) = %u\n", n, r.maxflips);
    return 0;
}
