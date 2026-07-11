/* fasta benchmark - C reference implementation
 * Matches the canonical Benchmarks Game algorithm exactly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define IM 139968
#define IA 3877
#define IC 29573
#define WIDTH 60

static uint32_t seed = 42;
#define uint32_rand() (seed = (seed * IA + IC) % IM)

static const char *iub = "acgtBDHKMNRSVWY";
static const float iub_p[] = {
    0.27, 0.12, 0.12, 0.27,
    0.02, 0.02, 0.02, 0.02, 0.02,
    0.02, 0.02, 0.02, 0.02, 0.02, 0.02
};

static const char *homosapiens = "acgt";
static const float homosapiens_p[] = {
    0.3029549426680,
    0.1979883004921,
    0.1975473066391,
    0.3015094502008
};

#define ALU "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"

static void repeat_fasta(const char *s, int count) {
    int len = (int)strlen(s);
    int pos = 0;
    int col = 0;
    int i;
    for (i = 0; i < count; i++) {
        putchar(s[pos]);
        col++;
        if (col == WIDTH) {
            putchar('\n');
            col = 0;
        }
        pos++;
        if (pos == len) pos = 0;
    }
    if (col > 0) putchar('\n');
}

// Match the pinned CLBG gcc #3 selection semantics exactly, but leave output
// scalar so this remains the naive local reference oracle.
static char *build_hash(const char *symbols, const float *probability) {
    int i, j;
    char *hash = malloc(IM);
    float sum = probability[0];
    const int len = (int)strlen(symbols);

    if (!hash) {
        exit(-1);
    }
    for (i = 0, j = 0; i < IM && j < len; i++) {
        float r = 1.0 * i / IM;
        if (r >= sum) {
            j++;
            sum += probability[j];
        }
        hash[i] = symbols[j];
    }
    return hash;
}

static void random_fasta(const char *symbols, const float *probability,
                         int count) {
    int col = 0;
    int i;
    char *hash = build_hash(symbols, probability);

    for (i = 0; i < count; i++) {
        putchar(hash[uint32_rand()]);
        col++;
        if (col == WIDTH) {
            putchar('\n');
            col = 0;
        }
    }
    if (col > 0) putchar('\n');
    free(hash);
}

int main(int argc, char **argv) {
    int n = 1000;
    if (argc > 1) n = atoi(argv[1]);

    printf(">ONE Homo sapiens alu\n");
    repeat_fasta(ALU, n * 2);
    printf(">TWO IUB ambiguity codes\n");
    random_fasta(iub, iub_p, n * 3);
    printf(">THREE Homo sapiens frequency\n");
    random_fasta(homosapiens, homosapiens_p, n * 5);

    return 0;
}
