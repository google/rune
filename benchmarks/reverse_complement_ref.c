/* reverse_complement_ref.c
 * Classic benchmarksgame reverse-complement, C reference.
 * Reads FASTA from stdin, emits reverse-complement wrapped at 60 cols.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LINE_LEN 60

static unsigned char comp[256];

static void init_comp(void) {
    /* Default: identity */
    for (int i = 0; i < 256; i++) comp[i] = (unsigned char)i;
    /* IUB complement map (case-insensitive input, uppercase output) */
    const char *from = "AaCcGgTtUuMmRrWwSsYyKkVvHhDdBbNn";
    const char *to   = "TTGGCCAAAAKMYWSRMBDHVN";  /* won't use this shortcut */

    /* Set explicitly */
    comp[(unsigned char)'A'] = comp[(unsigned char)'a'] = 'T';
    comp[(unsigned char)'C'] = comp[(unsigned char)'c'] = 'G';
    comp[(unsigned char)'G'] = comp[(unsigned char)'g'] = 'C';
    comp[(unsigned char)'T'] = comp[(unsigned char)'t'] = 'A';
    comp[(unsigned char)'U'] = comp[(unsigned char)'u'] = 'A';
    comp[(unsigned char)'M'] = comp[(unsigned char)'m'] = 'K';
    comp[(unsigned char)'R'] = comp[(unsigned char)'r'] = 'Y';
    comp[(unsigned char)'W'] = comp[(unsigned char)'w'] = 'W';
    comp[(unsigned char)'S'] = comp[(unsigned char)'s'] = 'S';
    comp[(unsigned char)'Y'] = comp[(unsigned char)'y'] = 'R';
    comp[(unsigned char)'K'] = comp[(unsigned char)'k'] = 'M';
    comp[(unsigned char)'V'] = comp[(unsigned char)'v'] = 'B';
    comp[(unsigned char)'H'] = comp[(unsigned char)'h'] = 'D';
    comp[(unsigned char)'D'] = comp[(unsigned char)'d'] = 'H';
    comp[(unsigned char)'B'] = comp[(unsigned char)'b'] = 'V';
    comp[(unsigned char)'N'] = comp[(unsigned char)'n'] = 'N';
}

static void emit_revcomp(unsigned char *seq, size_t len) {
    int col = 0;
    for (size_t i = len; i > 0; i--) {
        putchar(comp[seq[i - 1]]);
        col++;
        if (col == LINE_LEN) {
            putchar('\n');
            col = 0;
        }
    }
    if (col > 0) putchar('\n');
}

int main(void) {
    init_comp();

    size_t cap = 4096;
    unsigned char *seq = malloc(cap);
    size_t seqlen = 0;
    int in_seq = 0;

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t nread;

    while ((nread = getline(&line, &line_cap, stdin)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') {
            line[--nread] = '\0';
        }
        if (line[0] == '>') {
            /* Emit previous record */
            if (in_seq) {
                emit_revcomp(seq, seqlen);
            }
            /* Print header */
            fputs(line, stdout);
            putchar('\n');
            seqlen = 0;
            in_seq = 1;
        } else {
            /* Accumulate sequence */
            if (seqlen + (size_t)nread > cap) {
                while (seqlen + (size_t)nread > cap) cap *= 2;
                seq = realloc(seq, cap);
            }
            memcpy(seq + seqlen, line, (size_t)nread);
            seqlen += (size_t)nread;
        }
    }
    /* Emit last record */
    if (in_seq) {
        emit_revcomp(seq, seqlen);
    }

    free(seq);
    free(line);
    return 0;
}
