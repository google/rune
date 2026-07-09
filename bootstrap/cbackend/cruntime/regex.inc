/*
 * Rune C backend regex builtins, backed by PCRE2.
 *
 * string_methods.inc is emitted before this file by the compiler, so
 * rn_stralloc() and string_length() are already defined when this file is
 * compiled — do NOT redefine them here.
 *
 * PCRE2_CODE_UNIT_WIDTH must be defined before the pcre2.h include; both
 * live here so they are always emitted together.
 */
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

/*
 * rn_regex_count(pattern, text)
 *
 * Compile `pattern` and count the number of non-overlapping matches in
 * `text`.  `text` is a Rune headed string, so its true byte length is
 * read via string_length().
 *
 * After each match the search offset is advanced to the end of the match.
 * If a match is zero-width (start == end) we advance by one byte to avoid
 * an infinite loop on patterns like "a*".
 *
 * On pattern compile failure this aborts with a message to stderr; patterns
 * are always compiler-generated constants so this is a programming error,
 * not a runtime condition.
 */
static uint64_t rn_regex_count(const char *pattern, const char *text) {
    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *re = pcre2_compile(
        (PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
        0, &errcode, &erroffset, NULL);
    if (re == NULL) {
        PCRE2_UCHAR8 errbuf[256];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        fprintf(stderr, "rn_regex_count: PCRE2 compile error at offset %zu: %s\n",
                (size_t)erroffset, (char *)errbuf);
        exit(1);
    }

    pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);
    size_t text_len = (size_t)string_length(text);
    PCRE2_SIZE offset = 0;
    uint64_t count = 0;

    while (offset <= text_len) {
        int rc = pcre2_match(re, (PCRE2_SPTR)text, (PCRE2_SIZE)text_len,
                             offset, 0, md, NULL);
        if (rc < 0) {
            /* PCRE2_ERROR_NOMATCH or end-of-subject — we're done. */
            break;
        }
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(md);
        PCRE2_SIZE match_start = ovector[0];
        PCRE2_SIZE match_end   = ovector[1];
        count++;
        if (match_end > match_start) {
            offset = match_end;
        } else {
            /* Zero-width match: advance one byte to avoid infinite loop. */
            offset = match_start + 1;
        }
    }

    pcre2_match_data_free(md);
    pcre2_code_free(re);
    return count;
}

/*
 * rn_regex_replace(pattern, repl, text)
 *
 * Compile `pattern` and globally replace every match in `text` with `repl`,
 * returning a fresh Rune headed string built via rn_stralloc().
 *
 * Strategy for output-buffer sizing:
 *   We use PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_LITERAL.
 *   PCRE2_SUBSTITUTE_LITERAL tells PCRE2 to treat `repl` as a plain byte
 *   string with no $n back-reference or extended syntax, which is correct
 *   for the Rune compiler's usage (replacement strings are literals, never
 *   references).  This also makes our repl strings with characters like
 *   '(' or '$' safe without escaping.
 *
 *   We first try with an output buffer sized to 2×text_len + repl_len + 1,
 *   which is almost always enough.  If PCRE2 returns PCRE2_ERROR_NOMEMORY
 *   it writes the required length into outlen; we then allocate exactly that
 *   many bytes and retry.  After a successful call, outlen holds the ACTUAL
 *   number of bytes written (not counting the NUL terminator that pcre2
 *   appends).  Because rn_stralloc may have allocated more bytes than outlen
 *   (on the first path, where we sized generously), we patch the header's
 *   len field down to the real output length and re-NUL-terminate the buffer.
 *   This is valid because rn_stralloc gave the buffer a real rn_strhdr.
 */
static char *rn_regex_replace(const char *pattern, const char *repl,
                              const char *text) {
    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *re = pcre2_compile(
        (PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
        0, &errcode, &erroffset, NULL);
    if (re == NULL) {
        PCRE2_UCHAR8 errbuf[256];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        fprintf(stderr, "rn_regex_replace: PCRE2 compile error at offset %zu: %s\n",
                (size_t)erroffset, (char *)errbuf);
        exit(1);
    }

    size_t text_len = (size_t)string_length(text);
    size_t repl_len = (size_t)string_length(repl);

    /* Initial guess: generous upper bound. */
    size_t outlen = 2 * text_len + repl_len + 1;
    if (outlen < 64) outlen = 64;

    char *out = rn_stralloc(outlen);

    PCRE2_SIZE pcre_outlen = (PCRE2_SIZE)outlen;
    int rc = pcre2_substitute(
        re,
        (PCRE2_SPTR)text, (PCRE2_SIZE)text_len,
        /*startoffset=*/0,
        PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_LITERAL,
        /*match_data=*/NULL, /*mcontext=*/NULL,
        (PCRE2_SPTR)repl, (PCRE2_SIZE)repl_len,
        (PCRE2_UCHAR8 *)out, &pcre_outlen);

    if (rc == PCRE2_ERROR_NOMEMORY) {
        /*
         * pcre_outlen now holds the required output length (excluding NUL).
         * Allocate exactly that much and retry.
         */
        outlen = (size_t)pcre_outlen;
        out = rn_stralloc(outlen);
        pcre_outlen = (PCRE2_SIZE)outlen;
        rc = pcre2_substitute(
            re,
            (PCRE2_SPTR)text, (PCRE2_SIZE)text_len,
            /*startoffset=*/0,
            PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_LITERAL,
            /*match_data=*/NULL, /*mcontext=*/NULL,
            (PCRE2_SPTR)repl, (PCRE2_SIZE)repl_len,
            (PCRE2_UCHAR8 *)out, &pcre_outlen);
    }

    if (rc < 0) {
        PCRE2_UCHAR8 errbuf[256];
        pcre2_get_error_message(rc, errbuf, sizeof(errbuf));
        fprintf(stderr, "rn_regex_replace: pcre2_substitute error: %s\n",
                (char *)errbuf);
        exit(1);
    }

    /*
     * pcre_outlen is the number of bytes written (without NUL).  The
     * rn_stralloc'd buffer may be larger (generous initial estimate path).
     * Patch the header down to the real length and re-NUL-terminate so
     * string_length() and NUL-based readers both see the truth.
     */
    rn_strhdr *h = (rn_strhdr *)out - 1;
    h->len = (size_t)pcre_outlen;
    out[(size_t)pcre_outlen] = '\0';

    pcre2_code_free(re);
    return out;
}
