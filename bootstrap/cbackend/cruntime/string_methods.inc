/*
 * Rune C backend string method builtins.
 *
 * Strings are NUL-terminated heap or static char buffers with value
 * semantics: mutating methods (append, concat, resize, reverse) return a
 * fresh buffer and the call site assigns it back to the receiver.
 *
 * find/rfind are ported from the original compiler's runtime
 * (runtime/io.c) so offsets and the not-found result (the string length)
 * behave identically.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint64_t string_length(const char *s) {
  return s == NULL ? 0 : (uint64_t)strlen(s);
}

static inline char *string_append_char(const char *s, uint8_t c) {
  size_t len = s == NULL ? 0 : strlen(s);
  char *r = (char *)malloc(len + 2);
  if (len > 0) {
    memcpy(r, s, len);
  }
  r[len] = (char)c;
  r[len + 1] = '\0';
  return r;
}

// Duplicate a string into a fresh, writable heap buffer.  Assignments of
// string literals use this so that later writes (s[0] = c, append) do not
// mutate the static literal.
static inline char *string_dup(const char *s) {
  size_t len = s == NULL ? 0 : strlen(s);
  char *r = (char *)malloc(len + 1);
  if (len > 0) {
    memcpy(r, s, len);
  }
  r[len] = '\0';
  return r;
}

static inline char *string_concat(const char *a, const char *b) {
  size_t la = a == NULL ? 0 : strlen(a);
  size_t lb = b == NULL ? 0 : strlen(b);
  char *r = (char *)malloc(la + lb + 1);
  if (la > 0) {
    memcpy(r, a, la);
  }
  if (lb > 0) {
    memcpy(r + la, b, lb);
  }
  r[la + lb] = '\0';
  return r;
}

static inline char *string_resize(const char *s, uint64_t n) {
  size_t len = s == NULL ? 0 : strlen(s);
  char *r = (char *)calloc((size_t)n + 1, 1);
  memcpy(r, s, len < n ? len : (size_t)n);
  // Strings are NUL-terminated buffers, so growth must pad with a non-NUL
  // byte to actually extend the string (rn_freadInternal relies on strlen
  // reporting the resized capacity).
  if (len < n) {
    memset(r + len, ' ', (size_t)n - len);
  }
  return r;
}

// A one-character string holding the given byte.
static inline char *string_chr(uint8_t c) {
  char *r = (char *)malloc(2);
  r[0] = (char)c;
  r[1] = '\0';
  return r;
}

// A [lo:hi) slice of a string.  Out-of-range bounds clamp to the string
// length.
static inline char *string_slice(const char *s, uint64_t lo, uint64_t hi) {
  size_t len = s == NULL ? 0 : strlen(s);
  if (lo > len) {
    lo = len;
  }
  if (hi > len) {
    hi = len;
  }
  if (hi < lo) {
    hi = lo;
  }
  char *r = (char *)malloc(hi - lo + 1);
  memcpy(r, s + lo, hi - lo);
  r[hi - lo] = '\0';
  return r;
}

static inline char *string_reverse(const char *s) {
  size_t len = s == NULL ? 0 : strlen(s);
  char *r = (char *)malloc(len + 1);
  for (size_t i = 0; i < len; i++) {
    r[i] = s[len - 1 - i];
  }
  r[len] = '\0';
  return r;
}

static inline char *string_tohex(const char *s) {
  static const char kHexDigits[] = "0123456789abcdef";
  size_t len = s == NULL ? 0 : strlen(s);
  char *r = (char *)malloc(2 * len + 1);
  for (size_t i = 0; i < len; i++) {
    r[2 * i] = kHexDigits[((uint8_t)s[i] >> 4) & 0xf];
    r[2 * i + 1] = kHexDigits[(uint8_t)s[i] & 0xf];
  }
  r[2 * len] = '\0';
  return r;
}

static inline int string_hexdigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static inline char *string_fromhex(const char *s) {
  size_t len = s == NULL ? 0 : strlen(s);
  char *r = (char *)malloc(len / 2 + 1);
  size_t n = 0;
  for (size_t i = 0; i + 1 < len; i += 2) {
    int hi = string_hexdigit(s[i]);
    int lo = string_hexdigit(s[i + 1]);
    r[n++] = (char)((hi << 4) | lo);
  }
  r[n] = '\0';
  return r;
}

static inline char *string_xor(const char *a, const char *b) {
  size_t la = a == NULL ? 0 : strlen(a);
  size_t lb = b == NULL ? 0 : strlen(b);
  size_t n = la < lb ? la : lb;
  char *r = (char *)malloc(n + 1);
  for (size_t i = 0; i < n; i++) {
    r[i] = a[i] ^ b[i];
  }
  r[n] = '\0';
  return r;
}

static inline char *bool_tostring(int b) {
  return string_concat(b ? "true" : "false", "");
}

static inline char *uint_tostring(uint64_t v, uint64_t base) {
  static const char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  char buf[65];
  size_t n = 0;
  if (base < 2 || base > 36) {
    base = 10;
  }
  do {
    buf[n++] = kDigits[v % base];
    v /= base;
  } while (v != 0);
  char *r = (char *)malloc(n + 1);
  for (size_t i = 0; i < n; i++) {
    r[i] = buf[n - 1 - i];
  }
  r[n] = '\0';
  return r;
}

static inline char *int_tostring(int64_t v, uint64_t base) {
  if (v >= 0) {
    return uint_tostring((uint64_t)v, base);
  }
  char *digits = uint_tostring(-(uint64_t)v, base);
  char *r = string_concat("-", digits);
  free(digits);
  return r;
}

// sprintf into a freshly allocated string.
static inline char *string_sprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) {
    n = 0;
  }
  char *r = (char *)malloc((size_t)n + 1);
  va_start(ap, fmt);
  vsnprintf(r, (size_t)n + 1, fmt, ap);
  va_end(ap);
  return r;
}

// The bytes of |v| in little-endian order as a string of |nbytes| chars.
static inline char *int_tostring_le(uint64_t v, uint64_t nbytes) {
  char *r = (char *)malloc((size_t)nbytes + 1);
  for (uint64_t i = 0; i < nbytes; i++) {
    r[i] = (char)((v >> (8 * i)) & 0xff);
  }
  r[nbytes] = '\0';
  return r;
}

// The bytes of |v| in big-endian order as a string of |nbytes| chars.
static inline char *int_tostring_be(uint64_t v, uint64_t nbytes) {
  char *r = (char *)malloc((size_t)nbytes + 1);
  for (uint64_t i = 0; i < nbytes; i++) {
    r[i] = (char)((v >> (8 * (nbytes - 1 - i))) & 0xff);
  }
  r[nbytes] = '\0';
  return r;
}

// Find a sub-string in a string, starting at the offset.  Return the length
// of the string if |needle| is not found in |haystack|.
static inline uint64_t string_find(const char *haystack, const char *needle,
                                   uint64_t offset) {
  uint64_t length = string_length(haystack);
  uint64_t needleLength = string_length(needle);
  if (needleLength > length || offset > length - needleLength) {
    return length;
  }
  const char *p = haystack + offset;
  const char *q = needle;
  for (uint64_t i = offset; i < length; i++) {
    if (*p++ == *q) {
      const char *r = p;
      const char *s = q + 1;
      uint64_t j;
      for (j = 1; j < needleLength && *r++ == *s++; j++);
      if (j == needleLength) {
        return i;
      }
    }
  }
  return length;
}

// Reverse-find a sub-string in a string, starting at the offset.  Return the
// length of the string if |needle| is not found in |haystack|.
static inline uint64_t string_rfind(const char *haystack, const char *needle,
                                    uint64_t offset) {
  uint64_t length = string_length(haystack);
  uint64_t needleLength = string_length(needle);
  if (needleLength > length || offset > length - needleLength) {
    return length;
  }
  const char *p = haystack + length - needleLength;
  const char *q = needle;
  for (uint64_t i = offset; i < length; i++) {
    if (*p-- == *q) {
      const char *r = p + 2;
      const char *s = q + 1;
      uint64_t j;
      for (j = 1; j < needleLength && *r++ == *s++; j++);
      if (j == needleLength) {
        return length - needleLength - (i - offset);
      }
    }
  }
  return length;
}

// Random printable string of n bytes (nonce generation).
static inline char *rn_rand_string(uint64_t n) {
  char *s = (char *)malloc(n + 1);
  uint64_t i;
  for (i = 0; i < n; i++) {
    s[i] = (char)(33 + (rand() % 94));
  }
  s[n] = '\0';
  return s;
}

// Builtin max/min on 64-bit unsigned values.
static inline uint64_t rn_max(uint64_t a, uint64_t b) {
  return a > b ? a : b;
}
static inline uint64_t rn_min(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}
