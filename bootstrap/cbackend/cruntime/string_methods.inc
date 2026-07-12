/*
 * Rune C backend string method builtins.
 *
 * Strings are length-headed byte buffers with value semantics: a hidden
 * rn_strhdr (magic + byte length) sits immediately before the char data,
 * exactly like the array_t "struct hack".  string_t stays `char *`
 * pointing at the data, so C library calls and %s still work for
 * NUL-free content (the buffer is also NUL-terminated).  Every nonnull
 * Rune-visible string has this header; raw C strings must cross the ABI
 * through rn_strfromc().  This invariant lets string_length read the true
 * binary length without probing before an arbitrary borrowed pointer.
 * Mutating methods (append, concat, resize, reverse) return a fresh buffer
 * and the call site assigns it back to the receiver.
 *
 * find/rfind are ported from the original compiler's runtime
 * (runtime/io.c) so offsets and the not-found result (the string length)
 * behave identically.
 */

#include <stdarg.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Length header placed immediately before a string's char data.  The magic
// diagnoses violations of the headed Rune-string invariant; it is not used
// to guess whether an arbitrary pointer happens to have a header.
#define RN_STR_MAGIC UINT64_C(0x726E537472486472)         /* "rnStrHdr" */
#define RN_STATIC_STR_MAGIC UINT64_C(0x726E537472537461)  /* "rnStrSta" */

typedef struct {
  uint64_t magic;
  size_t len;
} rn_strhdr;

#if defined(__x86_64__)
_Static_assert(sizeof(rn_strhdr) == 16u,
               "x86-64 Rune strings require a 16-byte header");
#endif

// Allocate a headed, NUL-terminated string buffer of |len| bytes and
// return a pointer to its data.  Callers fill data[0..len) themselves.
static inline char *rn_stralloc(size_t len) {
  if (len > SIZE_MAX - sizeof(rn_strhdr) - 1u) {
    raise("StringSizeOverflow");
  }
  rn_strhdr *h = (rn_strhdr *)malloc(sizeof(rn_strhdr) + len + 1);
  if (h == NULL) {
    raise("OutOfMemory");
  }
  h->magic = RN_STR_MAGIC;
  h->len = len;
  char *data = (char *)(h + 1);
  data[len] = '\0';
  return data;
}

static inline const rn_strhdr *rn_strheader(const char *s) {
  // Byte arithmetic is valid for both heap allocations and the data member
  // of an emitted static literal object. Typed pointer subtraction would
  // incorrectly require data to be one-past an rn_strhdr array.
  const rn_strhdr *h = (const rn_strhdr *)(
      (const unsigned char *)s - sizeof(rn_strhdr));
  assert(h->magic == RN_STR_MAGIC || h->magic == RN_STATIC_STR_MAGIC);
  return h;
}

// Copy a conventional NUL-terminated C string across the Rune ABI boundary.
// strlen is intentionally confined to the raw pointer before the headed Rune
// value exists; Rune operations never inspect memory before that raw pointer.
static inline char *rn_strfromc(const char *s) {
  size_t len = s == NULL ? 0u : strlen(s);
  char *r = rn_stralloc(len);
  if (len > 0u) {
    memcpy(r, s, len);
  }
  return r;
}

// Release a known-owned headed temporary.  General Rune string values still
// follow the existing process-lifetime ownership model.
static inline void rn_strfree(char *s) {
  if (s != NULL) {
    const rn_strhdr *h = rn_strheader(s);
    assert(h->magic == RN_STR_MAGIC);
    free((void *)h);
  }
}

static inline uint64_t string_length(const char *s) {
  if (s == NULL) {
    return 0;
  }
  return (uint64_t)rn_strheader(s)->len;
}

// Binary-safe lexicographic comparison.  Keep null distinct from the empty
// headed string, matching isequal_string_t's historical null semantics.
static inline int string_compare(const char *a, const char *b) {
  if (a == b) {
    return 0;
  }
  if (a == NULL) {
    return -1;
  }
  if (b == NULL) {
    return 1;
  }
  size_t a_len = (size_t)string_length(a);
  size_t b_len = (size_t)string_length(b);
  size_t common = a_len < b_len ? a_len : b_len;
  int compared = common == 0u ? 0 : memcmp(a, b, common);
  if (compared != 0) {
    return compared;
  }
  return a_len < b_len ? -1 : (a_len > b_len ? 1 : 0);
}

static inline char *string_append_char(const char *s, uint8_t c) {
  size_t len = string_length(s);
  char *r = rn_stralloc(len + 1);
  if (len > 0) {
    memcpy(r, s, len);
  }
  r[len] = (char)c;
  return r;
}

// Duplicate a string into a fresh, writable heap buffer.  Assignments of
// string literals use this so that later writes (s[0] = c, append) do not
// mutate the static literal.
static inline char *string_dup(const char *s) {
  size_t len = string_length(s);
  char *r = rn_stralloc(len);
  if (len > 0) {
    memcpy(r, s, len);
  }
  return r;
}

// Set one byte through an assignable string slot. Static literal storage is
// immutable and shared, so only that case adopts a fresh heap duplicate;
// ordinary heap strings retain their existing in-place mutation semantics.
static inline uint8_t string_set_index(char **receiver, uint64_t index,
                                       uint8_t value) {
  if (receiver == NULL || *receiver == NULL ||
      index >= string_length(*receiver)) {
    raise("IndexOutOfBounds");
  }
  if (rn_strheader(*receiver)->magic == RN_STATIC_STR_MAGIC) {
    *receiver = string_dup(*receiver);
  }
  (*receiver)[index] = (char)value;
  return value;
}

// An rvalue receiver has no caller-owned slot that can adopt a COW duplicate.
// Heap pointers still mutate in place. A static value is copied, written, and
// discarded, matching the old per-evaluation literal's unobservable write.
static inline uint8_t string_set_index_value(char *receiver, uint64_t index,
                                             uint8_t value) {
  return string_set_index(&receiver, index, value);
}

static inline char *string_concat(const char *a, const char *b) {
  size_t la = string_length(a);
  size_t lb = string_length(b);
  char *r = rn_stralloc(la + lb);
  if (la > 0) {
    memcpy(r, a, la);
  }
  if (lb > 0) {
    memcpy(r + la, b, lb);
  }
  return r;
}

static inline char *string_resize(const char *s, uint64_t n) {
  size_t len = string_length(s);
  char *r = rn_stralloc((size_t)n);
  size_t keep = len < (size_t)n ? len : (size_t)n;
  if (keep > 0) {
    memcpy(r, s, keep);
  }
  // Strings are NUL-terminated buffers, so growth pads with a non-NUL
  // byte (the header carries the true length, but rn_freadInternal and
  // other strlen-based readers rely on the resized capacity being
  // reachable by strlen too).
  if (keep < (size_t)n) {
    memset(r + keep, ' ', (size_t)n - keep);
  }
  return r;
}

static inline char *string_chr(uint8_t c) {
  char *r = rn_stralloc(1);
  r[0] = (char)c;
  return r;
}

// A [lo:hi) slice of a string.  Out-of-range bounds clamp to the string
// length.
static inline char *string_slice(const char *s, uint64_t lo, uint64_t hi) {
  size_t len = string_length(s);
  if (lo > len) {
    lo = len;
  }
  if (hi > len) {
    hi = len;
  }
  if (hi < lo) {
    hi = lo;
  }
  char *r = rn_stralloc(hi - lo);
  memcpy(r, s + lo, hi - lo);
  return r;
}

static inline char *string_reverse(const char *s) {
  size_t len = string_length(s);
  char *r = rn_stralloc(len);
  for (size_t i = 0; i < len; i++) {
    r[i] = s[len - 1 - i];
  }
  return r;
}

static inline char *string_tohex(const char *s) {
  static const char kHexDigits[] = "0123456789abcdef";
  size_t len = string_length(s);
  char *r = rn_stralloc(2 * len);
  for (size_t i = 0; i < len; i++) {
    r[2 * i] = kHexDigits[((uint8_t)s[i] >> 4) & 0xf];
    r[2 * i + 1] = kHexDigits[(uint8_t)s[i] & 0xf];
  }
  return r;
}

static inline int string_hexdigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static inline char *string_fromhex(const char *s) {
  size_t len = string_length(s);
  char *r = rn_stralloc(len / 2);
  size_t n = 0;
  for (size_t i = 0; i + 1 < len; i += 2) {
    int hi = string_hexdigit(s[i]);
    int lo = string_hexdigit(s[i + 1]);
    r[n++] = (char)((hi << 4) | lo);
  }
  return r;
}

static inline char *string_xor(const char *a, const char *b) {
  size_t la = string_length(a);
  size_t lb = string_length(b);
  size_t n = la < lb ? la : lb;
  char *r = rn_stralloc(n);
  for (size_t i = 0; i < n; i++) {
    r[i] = a[i] ^ b[i];
  }
  return r;
}

static inline char *bool_tostring(int b) {
  const char *word = b ? "true" : "false";
  size_t len = b ? 4u : 5u;
  char *result = rn_stralloc(len);
  memcpy(result, word, len);
  return result;
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
  char *r = rn_stralloc(n);
  for (size_t i = 0; i < n; i++) {
    r[i] = buf[n - 1 - i];
  }
  return r;
}

static inline char *int_tostring(int64_t v, uint64_t base) {
  if (v >= 0) {
    return uint_tostring((uint64_t)v, base);
  }
  char *digits = uint_tostring(-(uint64_t)v, base);
  size_t digit_count = (size_t)string_length(digits);
  char *result = rn_stralloc(digit_count + 1u);
  result[0] = '-';
  if (digit_count > 0u) {
    memcpy(result + 1, digits, digit_count);
  }
  rn_strfree(digits);
  return result;
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
  char *r = rn_stralloc((size_t)n);
  va_start(ap, fmt);
  vsnprintf(r, (size_t)n + 1, fmt, ap);
  va_end(ap);
  return r;
}

// The bytes of |v| in little-endian order as a string of |nbytes| chars.
static inline char *int_tostring_le(uint64_t v, uint64_t nbytes) {
  char *r = rn_stralloc((size_t)nbytes);
  for (uint64_t i = 0; i < nbytes; i++) {
    r[i] = (char)((v >> (8 * i)) & 0xff);
  }
  return r;
}

// The bytes of |v| in big-endian order as a string of |nbytes| chars.
static inline char *int_tostring_be(uint64_t v, uint64_t nbytes) {
  char *r = rn_stralloc((size_t)nbytes);
  for (uint64_t i = 0; i < nbytes; i++) {
    r[i] = (char)((v >> (8 * (nbytes - 1 - i))) & 0xff);
  }
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
  char *s = rn_stralloc((size_t)n);
  uint64_t i;
  for (i = 0; i < n; i++) {
    s[i] = (char)(33 + (rand() % 94));
  }
  return s;
}

// Random unsigned integer in [0, bound); bound 0 returns 0.
static inline uint64_t rn_rand_range(uint64_t bound) {
  uint64_t r;
  if (bound == 0) {
    return 0;
  }
  r = ((uint64_t)rand() << 32) ^ (uint64_t)rand();
  return r % bound;
}

// Builtin max/min on 64-bit unsigned values.
static inline uint64_t rn_max(uint64_t a, uint64_t b) {
  return a > b ? a : b;
}
static inline uint64_t rn_min(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

// "123".toUint(type, var ok): decimal parse with success flag.
static inline uint64_t rn_string_touint(const char *s, unsigned char *ok) {
  char *end;
  unsigned long long v = strtoull(s, &end, 10);
  *ok = (end != s && *end == '\0');
  return (uint64_t)v;
}
