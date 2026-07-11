// C reference implementation of spectral_norm benchmark.
// Uses the standard CLBG output format: exactly nine decimal places.
// Uses Rune's own Newton-Raphson sqrt (same algorithm as the generated C)
// to ensure bit-identical results.

#include <stdio.h>
#include <stdlib.h>

// Replicates Rune's rn_user_sqrt(squared, 3e-14).
static double rn_sqrt(double squared) {
    double guess = squared / 0.115;
    double nextGuess = 0.5 * (guess + squared / guess);
    double diff = nextGuess - guess;
    if (diff < 0.0) diff = -diff;
    while (diff > 2.9999999999999998e-14) {
        guess = nextGuess;
        nextGuess = 0.5 * (guess + squared / guess);
        diff = nextGuess - guess;
        if (diff < 0.0) diff = -diff;
    }
    return nextGuess;
}

static double evalA(unsigned long i, unsigned long j) {
    return (double)((i + j) * (i + j + 1) / 2 + i + 1);
}

static void Times(double *v, const double *u, unsigned long n) {
    for (unsigned long i = 0; i < n; i++) {
        v[i] = 0.0;
        for (unsigned long j = 0; j < n; j++) {
            v[i] += u[j] / evalA(i, j);
        }
    }
}

static void TimesTransp(double *v, const double *u, unsigned long n) {
    for (unsigned long i = 0; i < n; i++) {
        v[i] = 0.0;
        for (unsigned long j = 0; j < n; j++) {
            v[i] += u[j] / evalA(j, i);
        }
    }
}

static void ATimesTransp(double *v, double *u, unsigned long n) {
    double *x = (double *)malloc(n * sizeof(double));
    Times(x, u, n);
    TimesTransp(v, x, n);
    free(x);
}

int main(int argc, char *argv[]) {
    unsigned long N = 100;
    if (argc > 1) N = (unsigned long)atol(argv[1]);
    if (N == 0) {
        printf("NaN\n");
        return 0;
    }

    double *u = (double *)malloc(N * sizeof(double));
    double *v = (double *)malloc(N * sizeof(double));

    for (unsigned long i = 0; i < N; i++) u[i] = 1.0;

    // Fixed 10 power iterations (benchmarksgame standard)
    for (unsigned long iter = 0; iter < 10; iter++) {
        ATimesTransp(v, u, N);
        ATimesTransp(u, v, N);
    }

    double vBv = 0.0, vv = 0.0;
    for (unsigned long i = 0; i < N; i++) {
        vBv += u[i] * v[i];
        vv  += v[i] * v[i];
    }

    printf("%.9f\n", rn_sqrt(vBv / vv));

    free(u);
    free(v);
    return 0;
}
