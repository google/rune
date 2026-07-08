/* fasta benchmark - C reference implementation
 * Matches the canonical Benchmarks Game algorithm exactly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IM 139968
#define IA 3877
#define IC 29573
#define WIDTH 60

static int seed = 42;

static double gen_random(double max) {
    seed = (seed * IA + IC) % IM;
    return max * seed / IM;
}

typedef struct {
    char c;
    double p;
} sym_t;

static sym_t iub[] = {
    {'a', 0.27}, {'c', 0.12}, {'g', 0.12}, {'t', 0.27},
    {'B', 0.02}, {'D', 0.02}, {'H', 0.02}, {'K', 0.02},
    {'M', 0.02}, {'N', 0.02}, {'R', 0.02}, {'S', 0.02},
    {'V', 0.02}, {'W', 0.02}, {'Y', 0.02}
};
#define IUB_LEN (int)(sizeof(iub)/sizeof(iub[0]))

static sym_t homos[] = {
    {'a', 0.3029549426680},
    {'c', 0.1979883004921},
    {'g', 0.1975473066391},
    {'t', 0.3015094502008}
};
#define HOMOS_LEN (int)(sizeof(homos)/sizeof(homos[0]))

#define ALU "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAAA"

static void make_cumulative(sym_t *table, int n) {
    double cp = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        cp += table[i].p;
        table[i].p = cp;
    }
}

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

static void random_fasta(sym_t *table, int n, int count) {
    int col = 0;
    int i, j;
    for (i = 0; i < count; i++) {
        double p = gen_random(1.0);
        for (j = 0; j < n - 1 && table[j].p < p; j++)
            ;
        putchar(table[j].c);
        col++;
        if (col == WIDTH) {
            putchar('\n');
            col = 0;
        }
    }
    if (col > 0) putchar('\n');
}

int main(int argc, char **argv) {
    int n = 1000;
    if (argc > 1) n = atoi(argv[1]);

    make_cumulative(iub, IUB_LEN);
    make_cumulative(homos, HOMOS_LEN);

    printf(">ONE Homo sapiens alu\n");
    repeat_fasta(ALU, n * 2);
    printf(">TWO IUB ambiguity codes\n");
    random_fasta(iub, IUB_LEN, n * 3);
    printf(">THREE Homo sapiens frequency\n");
    random_fasta(homos, HOMOS_LEN, n * 5);

    return 0;
}
