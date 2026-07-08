// C reference implementation of spectral_norm benchmark.
// Matches output of benchmarks/spectral_norm.rn byte-for-byte.
//
// Uses Rune's float formatting: %.6e, strip trailing mantissa zeros,
// omit exponent when zero (else write e<n> with no + sign).
// Uses Rune's own Newton-Raphson sqrt (same algorithm as the generated C)
// to ensure bit-identical results.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Replicates Rune's tostring_rune_float(d, 6) formatting.
static void print_rune_float(double d) {
    char buf[64], out[64];
    // Rune treats non-finite and overflow as NaN
    if (d != d || d > 1.7976931348623157e+308 || d < -1.7976931348623157e+308) {
        printf("NaN");
        return;
    }
    if (d == 0.0) { printf("0.0"); return; }
    snprintf(buf, sizeof(buf), "%.6e", d);
    char *p = buf, *q = out;
    // Copy mantissa part (up to 'e')
    while (*p != 'e' && *p != '\0') *q++ = *p++;
    // Strip trailing zeros from mantissa, keeping at least one decimal digit
    while (q[-1] == '0' && q[-2] != '.') q--;
    // Parse exponent
    long exponent = 0;
    int expSign = 1;
    if (*p == 'e') {
        p++;
        if (*p == '+') p++;
        else if (*p == '-') { expSign = -1; p++; }
        while (*p >= '0' && *p <= '9') {
            exponent = exponent * 10 + (*p - '0');
            p++;
        }
        exponent *= expSign;
    }
    if (exponent != 0) {
        snprintf(q, sizeof(out) - (size_t)(q - out), "e%ld", exponent);
    } else {
        *q = '\0';
    }
    printf("%s\n", out);
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

    print_rune_float(rn_sqrt(vBv / vv));

    free(u);
    free(v);
    return 0;
}
