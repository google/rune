/* k-nucleotide benchmark reference - The Computer Language Benchmarks Game */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { INITIAL_DNA_CAPACITY = 64 * 1024 };

static char *dna;
static size_t dna_len;
static size_t dna_capacity;

typedef struct {
    char key[20];
    long count;
} kmer_t;

/* At most 4^k entries; k<=2 means at most 16. Use a generous cap. */
static kmer_t kmers[65536];
static int num_kmers = 0;

static int cmp_kmer(const void *a, const void *b) {
    const kmer_t *ka = (const kmer_t *)a;
    const kmer_t *kb = (const kmer_t *)b;
    if (ka->count != kb->count)
        return (ka->count > kb->count) ? -1 : 1;  /* descending count */
    return strcmp(ka->key, kb->key);               /* ascending alpha */
}

static void count_kmers(int k) {
    num_kmers = 0;
    char key[20];
    for (size_t i = 0; i + (size_t)k <= dna_len; i++) {
        memcpy(key, dna + i, k);
        key[k] = '\0';
        int found = 0;
        for (int j = 0; j < num_kmers; j++) {
            if (strcmp(kmers[j].key, key) == 0) {
                kmers[j].count++;
                found = 1;
                break;
            }
        }
        if (!found) {
            strcpy(kmers[num_kmers].key, key);
            kmers[num_kmers].count = 1;
            num_kmers++;
        }
    }
}

int main(void) {
    char line[4096];
    int in_three = 0;

    dna_capacity = INITIAL_DNA_CAPACITY;
    dna = malloc(dna_capacity);
    if (!dna) {
        perror("malloc");
        return 1;
    }

    while (fgets(line, sizeof(line), stdin)) {
        int len = (int)strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len == 0) continue;
        if (line[0] == '>') {
            if (strncmp(line, ">THREE", 6) == 0)
                in_three = 1;
            else
                in_three = 0;
        } else if (in_three) {
            size_t required = dna_len + (size_t)len;
            if (required > dna_capacity) {
                while (required > dna_capacity)
                    dna_capacity *= 2;
                char *grown = realloc(dna, dna_capacity);
                if (!grown) {
                    perror("realloc");
                    free(dna);
                    return 1;
                }
                dna = grown;
            }
            for (int i = 0; i < len; i++)
                dna[dna_len++] = (char)toupper((unsigned char)line[i]);
        }
    }

    /* k=1 frequencies */
    count_kmers(1);
    qsort(kmers, num_kmers, sizeof(kmer_t), cmp_kmer);
    for (int i = 0; i < num_kmers; i++)
        printf("%s %.3f\n", kmers[i].key, 100.0 * kmers[i].count / dna_len);
    printf("\n");

    /* k=2 frequencies */
    count_kmers(2);
    qsort(kmers, num_kmers, sizeof(kmer_t), cmp_kmer);
    for (int i = 0; i < num_kmers; i++)
        printf("%s %.3f\n", kmers[i].key, 100.0 * kmers[i].count / (dna_len - 1));
    printf("\n");

    /* Specific sequences */
    const char *seqs[] = {
        "GGT", "GGTA", "GGTATT", "GGTATTTTAATT", "GGTATTTTAATTTATAGT"
    };
    for (int s = 0; s < 5; s++) {
        int k = (int)strlen(seqs[s]);
        long cnt = 0;
        for (size_t i = 0; i + (size_t)k <= dna_len; i++) {
            if (memcmp(dna + i, seqs[s], k) == 0)
                cnt++;
        }
        printf("%ld\t%s\n", cnt, seqs[s]);
    }

    free(dna);
    return 0;
}
