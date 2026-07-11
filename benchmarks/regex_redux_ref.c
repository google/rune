/* regex_redux_ref.c
 * The Computer Language Benchmarks Game — regex-redux
 * https://benchmarksgame-team.pages.debian.net/benchmarksgame/
 *
 * C reference using PCRE2.  Reads all of stdin, strips FASTA header/newline
 * lines, counts nine variant-pattern families, applies the five current CLBG
 * magic substitutions, then prints lengths.
 *
 * Build:
 *   clang -O3 benchmarks/regex_redux_ref.c -lpcre2-8 -o /tmp/rr_ref
 * Run:
 *   benchmarks/fasta 1000 | /tmp/rr_ref
 */

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------ */

static char *buf_read_stdin(size_t *out_len) {
    size_t cap = 65536;
    size_t len = 0;
    char  *buf = malloc(cap);
    if (!buf) { perror("malloc"); exit(1); }

    int c;
    while ((c = getchar_unlocked()) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) { perror("realloc"); exit(1); }
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

/* Replace all non-overlapping matches of pattern in src (length srclen) with
 * repl (length repllen).  Returns a newly-malloc'd string; sets *outlen. */
static char *regex_replace(const char *pattern,
                           const char *repl, size_t repllen,
                           const char *src,  size_t srclen,
                           size_t *outlen) {
    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
                                   0, &errcode, &erroffset, NULL);
    if (!re) {
        PCRE2_UCHAR errbuf[256];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        fprintf(stderr, "PCRE2 compile error at %zu: %s\n",
                erroffset, (char *)errbuf);
        exit(1);
    }
    pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);

    size_t outcap = srclen + 64;
    char  *out    = malloc(outcap);
    size_t outpos = 0;
    size_t srcpos = 0;

    while (srcpos <= srclen) {
        int rc = pcre2_match(re, (PCRE2_SPTR)src, srclen, srcpos, 0, md, NULL);
        if (rc < 0) break;  /* no match */

        PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
        size_t mstart  = ov[0];
        size_t mend    = ov[1];

        /* Copy literal segment before match */
        size_t seglen = mstart - srcpos;
        if (outpos + seglen + repllen + 1 >= outcap) {
            while (outpos + seglen + repllen + 1 >= outcap)
                outcap *= 2;
            out = realloc(out, outcap);
        }
        memcpy(out + outpos, src + srcpos, seglen);
        outpos += seglen;

        /* Emit replacement */
        memcpy(out + outpos, repl, repllen);
        outpos += repllen;

        srcpos = mend;
        if (mend == mstart) srcpos++;  /* avoid infinite loop on zero-length match */
    }

    /* Copy tail */
    size_t taillen = srclen - srcpos;
    if (outpos + taillen + 1 >= outcap) {
        outcap = outpos + taillen + 2;
        out = realloc(out, outcap);
    }
    memcpy(out + outpos, src + srcpos, taillen);
    outpos += taillen;
    out[outpos] = '\0';
    *outlen = outpos;

    pcre2_match_data_free(md);
    pcre2_code_free(re);
    return out;
}

/* Count non-overlapping matches of pattern in src (length srclen). */
static size_t regex_count(const char *pattern, const char *src, size_t srclen) {
    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
                                   0, &errcode, &erroffset, NULL);
    if (!re) {
        PCRE2_UCHAR errbuf[256];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        fprintf(stderr, "PCRE2 compile error at %zu: %s\n",
                erroffset, (char *)errbuf);
        exit(1);
    }
    pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);

    size_t count  = 0;
    size_t srcpos = 0;
    while (srcpos <= srclen) {
        int rc = pcre2_match(re, (PCRE2_SPTR)src, srclen, srcpos, 0, md, NULL);
        if (rc < 0) break;
        PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
        count++;
        srcpos = ov[1];
        if (ov[1] == ov[0]) srcpos++;  /* zero-length guard */
    }

    pcre2_match_data_free(md);
    pcre2_code_free(re);
    return count;
}

/* ---- main --------------------------------------------------------------- */

int main(void) {
    /* 1. Read all stdin */
    size_t ilen;
    char *input = buf_read_stdin(&ilen);

    /* 2. Strip FASTA descriptor lines and all newlines */
    size_t clen;
    char *seq = regex_replace(">[^\n]*\n|\n", "", 0, input, ilen, &clen);
    free(input);

    /* 3. Count nine variant-pattern families */
    static const char *patterns[] = {
        "agggtaaa|tttaccct",
        "[cgt]gggtaaa|tttaccc[acg]",
        "a[act]ggtaaa|tttacc[agt]t",
        "ag[act]gtaaa|tttac[agt]ct",
        "agg[act]taaa|ttta[agt]cct",
        "aggg[acg]aaa|ttt[cgt]ccct",
        "agggt[cgt]aa|tt[acg]accct",
        "agggta[cgt]a|t[acg]taccct",
        "agggtaa[cgt]|[acg]ttaccct",
    };
    int npatterns = (int)(sizeof(patterns) / sizeof(patterns[0]));
    for (int i = 0; i < npatterns; i++) {
        size_t cnt = regex_count(patterns[i], seq, clen);
        printf("%s %zu\n", patterns[i], cnt);
    }

    /* 4. Apply the five CLBG magic substitutions in order, chaining results. */
    static const char *subst_pat[] = {
        "tHa[Nt]",
        "aND|caN|Ha[DS]|WaS",
        "a[NSt]|BY",
        "<[^>]*>",
        "\\|[^|][^|]*\\|",
    };
    static const char *subst_repl[] = { "<4>", "<3>", "<2>", "|", "-" };
    int nsubsts = (int)(sizeof(subst_pat) / sizeof(subst_pat[0]));

    char  *cur    = seq;
    size_t curlen = clen;
    int    first  = 1;
    for (int i = 0; i < nsubsts; i++) {
        size_t newlen;
        char *next = regex_replace(subst_pat[i], subst_repl[i], strlen(subst_repl[i]),
                                   cur, curlen, &newlen);
        if (!first) free(cur);
        first = 0;
        cur    = next;
        curlen = newlen;
    }
    size_t slen = curlen;

    /* 5. Print lengths */
    printf("\n%zu\n%zu\n%zu\n", ilen, clen, slen);

    /* Cleanup */
    free(cur);
    free(seq);
    return 0;
}
