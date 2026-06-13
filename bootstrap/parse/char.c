#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <inttypes.h>

typedef uint8_t bool_t;
#define false (0)
#define true (1)

typedef char* string_t;

#define STRINGIFY(s) STR(s)
#define STR(s) #s


#if defined(DBL_DECIMAL_DIG)
#  define DBL_SIG_DIGITS STRINGIFY(DBL_DECIMAL_DIG)
#  define FLT_SIG_DIGITS STRINGIFY(FLT_DECIMAL_DIG)
#elif defined(DECIMAL_DIG)
#  define DBL_SIG_DIGITS STRINGIFY(DECIMAL_DIG)
#  define FLT_SIG_DIGITS STRINGIFY(DECIMAL_DIG)
#else
#  define DBL_SIG_DIGITS STRINGIFY(DBL_DIG + 3)
#  define FLT_SIG_DIGITS STRINGIFY(FLT_DIG + 3)
#endif


#define RN_MAX_TRY_DEPTH 64
static jmp_buf rn_try_stack[RN_MAX_TRY_DEPTH];
static uint32_t rn_try_depth = 0;
static char *rn_raised_code = "";
static char *rn_raised_message = "";

#define RN_TRY() (setjmp(rn_try_stack[rn_try_depth++]) == 0)

static void raise_with_code(const char *code, const char *message) {
  rn_raised_code = (char *)code;
  rn_raised_message = (char *)message;
  if (rn_try_depth > 0) {
    longjmp(rn_try_stack[--rn_try_depth], 1);
  }
  printf("%s%s Exception Raised, aborting\n", code, message);
  abort();
}

static void raise(const char *error) {
  raise_with_code("", error);
}

static inline int rn_code_matches(const char *code) {
  return strcmp(rn_raised_code, code) == 0;
}
/*
 * Rune C backend global string writer.
 *
 * This is a chunk of code to inline into a rune-generated C files whenever
 * we want to use 'print' or 'println' statements. It provides a simple class
 * and methods for managing consecutive print statements into a string before
 * writing these to standard out.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <float.h>
#include <string.h>

/**
 * @brief StringWriter class
 *
 * The StringWriter class manages a sequence of writes to a given char
 * buffer, making sure that we don't overwrite the end of the buffer, or
 * any previous writes.
 */
typedef struct StringWriter {
  char *buffer;
  char *wp;
  size_t space;
  size_t capacity;
} *StringWriter;


/**
 * @brief Initialize a StringWriter class
 *
 * @param size the required size of the char array.
 *
 * This method initializes a StringWriter object to allocate a char buffer
 * of a given size.
 */
static inline StringWriter StringWriter_open(size_t size) {
  StringWriter sw = (StringWriter)malloc(sizeof(struct StringWriter) + size);
  sw->buffer = (char *)sw + sizeof(struct StringWriter);
  memset(sw->buffer, 0, size);
  sw->wp = sw->buffer;
  sw->space = size - 1; /* leave room for final terminating null. */
  sw->capacity = size;
  return sw;
}


/**
 * @brief reset the StringWriter write pointer.
 *
 * @param sw the StringWriter object.
 *
 * After the contents of the StringWriter's buffer have been consumed (e.g.,
 * written to standard out, or a file, or included in some other string),
 * wipe its contents and reset the write pointer to the beginning, so that
 * it can be re-used.
 */
static inline void StringWriter_reset(StringWriter sw) {
  memset(sw->buffer, 0, sw->capacity);
  sw->wp = sw->buffer;
  sw->space = sw->capacity - 1;
}


/**
 * @brief Finalize a given StringWriter object.
 *
 * @param sw the StringWriter object to finalize.
 */
static inline void StringWriter_close(StringWriter sw) {
  free(sw);
}


/**
 * @brief Retrieve a const char* string from the StringWriter.
 *
 * @param sw the StringWriter object.
 * @returns a C string suitable for printing.
 */
static inline const char *StringWriter_string(StringWriter sw) {
  return sw->buffer;
}


/**
 * @brief Write a format string and arguments to a StringWriter.
 *
 * @param sw the StringWriter object
 * @param fmt the printf-like format string
 */
static inline void StringWriter_write(StringWriter sw, const char *fmt, ...) {
  va_list ap;
  int written;

  va_start(ap, fmt);
  written = vsnprintf(sw->wp, sw->space, fmt, ap);
  va_end(ap);

  if (written >= sw->space) {
    // output was truncated
    sw->wp += sw->space;
    sw->space = 0;
  } else {
    sw->wp += written;
    sw->space -= written;
  }
}


/**
 * @brief write a variable argument list to the stringwriter.
 *
 * @param sw the stringwriter structure managing a char buffer
 * @param fmt the string format to write
 * @param ap the variable argument list structure.
 */
static inline void StringWriter_writeap(StringWriter sw, const char *fmt, va_list ap) {
  int written;

  written = vsnprintf(sw->wp, sw->space, fmt, ap);

  if (written >= sw->space) {
    // output was truncated
    sw->wp += sw->space;
    sw->space = 0;
  } else {
    sw->wp += written;
    sw->space -= written;
  }
}


/**
 * @brief A single global string for writing print/println messages to.
 *
 * Note that this assumes that we are generating a single C source file.
 */
static char globalStringWriterBuffer[1024];


/**
 * @brief a single static global stringwriter instance
 *
 * This assumes that we are generating a single C source file.
 */
static struct StringWriter globalStringWriter = {
  globalStringWriterBuffer,
  globalStringWriterBuffer, sizeof(globalStringWriterBuffer) - 1,
  sizeof(globalStringWriterBuffer)
};


/**
 * @brief reset the global string writer buffer.
 *
 * Do this every time a new 'printf' is begun.
 */
void GlobalStringWriter_reset(void) {
  StringWriter_reset(&globalStringWriter);
}


/**
 * @brief Append a string to the string buffer.
 *
 * This will add a string to the end of the current write position inside
 * the buffer, truncating the string before it overflows, guaranteeing
 * at least one
 *
 * @param fmt The sprintf-formatted format string. Additional arguments
 *   (if any) should be listed after this one.
 */
void GlobalStringWriter_write(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  StringWriter_writeap(&globalStringWriter, fmt, ap);
  va_end(ap);
}


/**
 * @brief Return the string represented by the buffer.
 *
 * Note that this points directly to the internal buffer, and is not copied.
 */
const char *GlobalStringWriter_string(void) {
  return StringWriter_string(&globalStringWriter);
}


#define tostring_string_t GlobalStringWriter_write


/*
 * The following tostring_<type> helper methods are intended to be used as a
 * kind of 'static dispatch' mechanism for implementing a psuedo-polymorphic
 * 'tostring' method. The compiler will generate calls to tostring_<typename>()
 * methods, e.g., when generating a tostring_<tupletype> method out of
 * its component types.
 */
static inline void tostring_bool_t(bool_t b) {
  StringWriter_write(&globalStringWriter, b ? "true" : "false");
}

static inline void tostring_int8_t(int8_t i) {
  StringWriter_write(&globalStringWriter, "%" PRId8, i);
}

static inline void tostring_int16_t(int16_t i) {
  StringWriter_write(&globalStringWriter, "%" PRId16, i);
}

static inline void tostring_int32_t(int32_t i) {
  StringWriter_write(&globalStringWriter, "%" PRId32, i);
}

static inline void tostring_int64_t(int64_t i) {
  StringWriter_write(&globalStringWriter, "%" PRId64, i);
}

static inline void tostring_uint8_t(uint8_t u) {
  StringWriter_write(&globalStringWriter, "%" PRIu8, u);
}

static inline void tostring_uint16_t(uint16_t u) {
  StringWriter_write(&globalStringWriter, "%" PRIu16, u);
}

static inline void tostring_uint32_t(uint32_t u) {
  StringWriter_write(&globalStringWriter, "%" PRIu32, u);
}

static inline void tostring_uint64_t(uint64_t u) {
  StringWriter_write(&globalStringWriter, "%" PRIu64, u);
}

// Format a float the way Rune does: always scientific notation with a
// lower-case e and unpadded exponent, at least one fraction digit, and the
// specials 0.0, Inf and NaN.
static inline void tostring_rune_float(double d, int digits) {
  char buf[64];
  char out[64];
  if (d != d) {
    StringWriter_write(&globalStringWriter, "NaN");
    return;
  }
  if (d > DBL_MAX || d < -DBL_MAX) {
    // Rune prints all non-finite values as NaN, including infinities.
    StringWriter_write(&globalStringWriter, "NaN");
    return;
  }
  if (d == 0.0) {
    StringWriter_write(&globalStringWriter, "0.0");
    return;
  }
  snprintf(buf, sizeof(buf), "%.*e", digits, d);
  // buf looks like -1.234500e+02; rebuild it as -1.2345e2.
  char *p = buf;
  char *q = out;
  while (*p != 'e' && *p != '\0') {
    *q++ = *p++;
  }
  // Strip trailing zeros from the mantissa, keeping one fraction digit.
  while (q[-1] == '0' && q[-2] != '.') {
    q--;
  }
  long exponent = 0;
  int expSign = 1;
  if (*p == 'e') {
    p++;
    if (*p == '+') {
      p++;
    } else if (*p == '-') {
      expSign = -1;
      p++;
    }
    while (*p >= '0' && *p <= '9') {
      exponent = exponent * 10 + (*p - '0');
      p++;
    }
    exponent *= expSign;
  }
  if (exponent != 0) {
    snprintf(q, sizeof(out) - (q - out), "e%ld", exponent);
  } else {
    *q = '\0';
  }
  StringWriter_write(&globalStringWriter, "%s", out);
}

static inline void tostring_float_t(float f) {
  tostring_rune_float((double)f, 6);
}

static inline void tostring_double_t(double d) {
  tostring_rune_float(d, 6);
}

// Auto-generated <obj>.toString(): capture a tostring_<class> print into
// a fresh heap string.
static inline char *rn_dup_writer(void) {
  const char *s = GlobalStringWriter_string();
  char *r = (char *)malloc(strlen(s) + 1);
  strcpy(r, s);
  GlobalStringWriter_reset();
  return r;
}
#define RN_TOSTRING_RET(callexpr) (GlobalStringWriter_reset(), (callexpr), rn_dup_writer())
/**
 * @brief Internal implemenatation of Rune C backend arrays.
 *
 * Note that we use the "struct hack" to allocate the array data
 * immediately after the header.  We pass the pointer to the first
 * element so that we can simply use normal C array indexing.
 */

#define ARRAY_MAGIC (0xA99A73A6ul)

typedef struct {
  size_t magic_hdr;	 // safety check for arrays.
  size_t element_size;
  size_t capacity;
  size_t used;
  size_t magic_ftr;	 // safety check for arrays.
} array_t;


/* Rune C array allocation and initialization
 */

static inline void *array_alloc (size_t element_size, size_t num_initializers) {
    size_t capacity = num_initializers;
    // Find next-highest power of 2
    if (capacity == 0) {
      capacity = 16;
    } else {
      --capacity;
      capacity |= capacity >> 1;
      capacity |= capacity >> 2;
      capacity |= capacity >> 4;
      capacity |= capacity >> 8;
      capacity |= capacity >> 16;
      capacity |= capacity >> 32;
      ++capacity;
    }

    array_t *array = (array_t *)malloc(capacity * element_size + sizeof(array_t));
    if (array == NULL) {
       raise("Error: could not allocate array");
    }
    /* set up array informational struct. */
    array->element_size = element_size;
    array->capacity = capacity;
    array->used = num_initializers;
    array->magic_hdr = ARRAY_MAGIC;
    array->magic_ftr = ARRAY_MAGIC;
    return array + 1; /* skip the header */
}




/**
 * @brief resize a rune array
 *
 * @param a - the array to resize
 * @param count - the new size
 *
 * If count is 0, then this means we are emptying the
 * array. Otherwise, we need to find the lowest power-of-two that is
 * greater then (or equal to) the requested size. This approach means
 * that array extension operations are amortized linear time.
 */
static inline array_t *array_resize (array_t *a, size_t count) {
  if (count == 0) {
    a->used = 0;
    return a;
  }
  if (count < a->capacity) {
    return a;
  }

  /* Double the array size until we have enough. */
  size_t new_capacity = a->capacity;
  while (new_capacity < count) {
    new_capacity *= 2;
  }

  a->capacity = new_capacity;
  void *newmem = realloc(a, new_capacity * a->element_size + sizeof(array_t));
  if (newmem == NULL) {
    raise("Error: could not reallocate array");
  }

  return (array_t*)newmem;
}

/**
 * @brief Return the length of an array
 *
 * @param dest - the array to extend
 *
 * The ... args are used to swallow up generated artifacts in code generation. (i.e.,
 * the compiler will generate a specious empty tuple object because that's the parameter
 * passed into the length() method.
 *
 * @returns the number of elements in the array
 */
static inline size_t array_length (void *a, ...) {
  array_t *array = ((array_t *)a) - 1;

  assert(array->magic_hdr == ARRAY_MAGIC);
  assert(array->magic_ftr == ARRAY_MAGIC);

  return array->used;
}
/*
 * Rune C backend primitive type qualities
 *
 * This is a chunk of code to inline into a rune-generated C files whenever
 * we want to generate equality functions for non-primitive types, such as
 * a tuple. The equality for an aggregate type will be built from the equality
 * methods for its constituent parts. The following methods define the
 * equalities for the primitive types.
 */

#include <string.h>

static inline bool_t isequal_bool_t(bool_t a, bool_t b) {
   return a == b;
}

static inline bool_t isequal_string_t(string_t a, string_t b) {
   return !strcmp(a, b);
}

static inline bool_t isequal_int8_t(int8_t a, int8_t b) {
   return a == b;
}

static inline bool_t isequal_int16_t(int16_t a, int16_t b) {
   return a == b;
}

static inline bool_t isequal_int32_t(int32_t a, int32_t b) {
   return a == b;
}

static inline bool_t isequal_int64_t(int64_t a, int64_t b) {
   return a == b;
}

static inline bool_t isequal_uint8_t(uint8_t a, uint8_t b) {
   return a == b;
}

static inline bool_t isequal_uint16_t(uint16_t a, uint16_t b) {
   return a == b;
}

static inline bool_t isequal_uint32_t(uint32_t a, uint32_t b) {
   return a == b;
}

static inline bool_t isequal_uint64_t(uint64_t a, uint64_t b) {
   return a == b;
}

static inline bool_t isequal_float_t(float a, float b) {
   return a == b;
}

static inline bool_t isequal_double_t(double a, double b) {
   return a == b;
}
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

static uint64_t uintadd(uint64_t a, uint64_t b, int width) {
  uint64_t max = (uint64_t)(1ull << (width - 1)) - 1;
  if (a > (max - b)) raise("Overflow");
  return a + b;
}

static uint64_t uintsub(uint64_t a, uint64_t b, int width) {
  if (b > a) raise("Underflow");
  return a - b;
}

static inline uint32_t uintadd32(uint32_t a, uint32_t b) {
  return (uint32_t)uintadd(a, b, 32);
}

static inline uint8_t uintadd8(uint8_t a, uint8_t b) {
  return (uint8_t)uintadd(a, b, 8);
}

static inline uint64_t uintadd64(uint64_t a, uint64_t b) {
  return (uint64_t)uintadd(a, b, 64);
}

static inline uint32_t uintsub32(uint32_t a, uint32_t b) {
  return (uint32_t)uintsub(a, b, 32);
}

static inline uint8_t uintsub8(uint8_t a, uint8_t b) {
  return (uint8_t)uintsub(a, b, 8);
}

static inline uint64_t rotl64(uint64_t value, int distance) {
  return (value << distance) | (value >> (64 - distance));
}

typedef uint8_t *uint8_array_t;

uint8_t *uint8_array_init(size_t num_initializers, ...) {
  va_list ap;
  size_t i;
  
  uint8_t *a = (uint8_t*)array_alloc(sizeof(uint8_t), num_initializers);
  va_start(ap, num_initializers);
  for (i = 0; i < num_initializers; ++i) {
    a[i] = (uint8_t)va_arg(ap, int);
  }
  return a;
}

void tostring_uint8_array_t(uint8_t a[]) {
  array_t *array = ((array_t*)a) - 1;
  size_t i;
  if (a == 0) { tostring_string_t("[]"); return; }
  tostring_string_t("[");
  for(i = 0; i < array->used; ++i) {
    if (i > 0) tostring_string_t(", ");
    tostring_uint8_t(a[i]);
    tostring_string_t("u8");
  }
  tostring_string_t("]");
}

bool isequal_uint8_array_t(uint8_array_t a1, uint8_array_t a2) {
  array_t *s1a = ((array_t*)a1) - 1;
  array_t *s2a = ((array_t*)a2) - 1;
  size_t i;
  
  if (s1a->used != s2a->used) return false;
  for (i = 0; i < s1a->used; ++i) {
    if (!isequal_uint8_t(a1[i], a2[i])) return false;
  }
  return true;
}

uint8_array_t append_uint8_array_t(uint8_array_t a, uint8_t e) {
  array_t *array = ((array_t*)a) - 1;
  array = array_resize(array, array->used + 1);
  a = (uint8_t *)(array + 1);
  a[array->used] = e;
  array->used += 1;
  return a;
}

typedef uint8_array_t *uint8_array_array_t;

uint8_array_t *uint8_array_array_init(size_t num_initializers, ...) {
  va_list ap;
  size_t i;
  
  uint8_array_t *a = (uint8_array_t*)array_alloc(sizeof(uint8_array_t), num_initializers);
  va_start(ap, num_initializers);
  for (i = 0; i < num_initializers; ++i) {
    a[i] = (uint8_array_t)va_arg(ap, uint8_array_t);
  }
  return a;
}

void tostring_uint8_array_array_t(uint8_array_t a[]) {
  array_t *array = ((array_t*)a) - 1;
  size_t i;
  if (a == 0) { tostring_string_t("[]"); return; }
  tostring_string_t("[");
  for(i = 0; i < array->used; ++i) {
    if (i > 0) tostring_string_t(", ");
    tostring_uint8_array_t(a[i]);
  }
  tostring_string_t("]");
}

bool isequal_uint8_array_array_t(uint8_array_array_t a1, uint8_array_array_t a2) {
  array_t *s1a = ((array_t*)a1) - 1;
  array_t *s2a = ((array_t*)a2) - 1;
  size_t i;
  
  if (s1a->used != s2a->used) return false;
  for (i = 0; i < s1a->used; ++i) {
    if (!isequal_uint8_array_t(a1[i], a2[i])) return false;
  }
  return true;
}

uint8_array_array_t append_uint8_array_array_t(uint8_array_array_t a, uint8_array_t e) {
  array_t *array = ((array_t*)a) - 1;
  array = array_resize(array, array->used + 1);
  a = (uint8_array_t *)(array + 1);
  a[array->used] = e;
  array->used += 1;
  return a;
}

static uint8_array_array_t table;

static uint32_t pos;

static string_t sch_u195_u182n;

typedef struct Char_t {
  uint32_t pos;
  uint8_t len;
  bool_t valid;
} Char_t;

Char_t Char(uint32_t pos, uint8_t len, bool_t valid) {
  Char_t str = {0};
  str.pos = pos;
  str.len = len;
  str.valid = valid;
  return str;
}

void tostring_Char_t(Char_t str) {
  tostring_string_t("(");
  tostring_uint32_t(str.pos);
  tostring_string_t("u32");
  tostring_string_t(", ");
  tostring_uint8_t(str.len);
  tostring_string_t("u8");
  tostring_string_t(", ");
  tostring_bool_t(str.valid);
  tostring_string_t(")");
}

bool isequal_Char_t(Char_t s1, Char_t s2) {
  return isequal_uint32_t(s1.pos, s2.pos)
      && isequal_uint8_t(s1.len, s2.len)
      && isequal_bool_t(s1.valid, s2.valid);
}

static Char_t char;

string_t chr_u8(uint8_t x) {
  return (string_t)(uint8_array_init(1ul, (uint8_t)((uint8_t)(x))));
}


static uint8_t c;

bool_t isValidAsciiInRuneFile_h2(string_t text, uint32_t pos) {
  c = text[pos];
  if (c >= 32u && c <= 126u) {
    return true;
  }
  
  return isequal_uint8_t(c, 10u) || isequal_uint8_t(c, 13u) || isequal_uint8_t(c, 9u);
}


bool_t encodingIsOverlong(string_t text, uint32_t pos, uint8_t len) {
  if (isequal_uint8_t(len, 2ul)) {
    return isequal_uint8_t(text[pos] & 30ul, 0ul);
  }
  
  if (isequal_uint8_t(len, 3ul)) {
    return isequal_uint8_t(text[pos] & 15ul, 0ul) && isequal_uint8_t(text[uintadd32((uint32_t)(pos), 1ul)] & 32ul, 0ul);
  }
  
  return isequal_uint8_t(text[pos] & 7ul, 0ul) && isequal_uint8_t(text[uintadd32((uint32_t)(pos), 1ul)] & 48ul, 0ul);
}


bool_t isTrojanSourceChar(string_t text, uint32_t pos, uint8_t len) {
  uint8_t c1;
  uint8_t c3;
  uint8_t c2;
  c1 = text[pos];
  if (isequal_uint8_t(c1, 226ul)) {
    c2 = text[uintadd32((uint32_t)(pos), 1ul)];
    c3 = text[uintadd32((uint32_t)(pos), 2ul)];
    if (isequal_uint8_t(c2, 128ul)) {
      if (c3 >= 170ul && c3 <= 174ul) {
        return true;
      }
      
    }
    
    else if (isequal_uint8_t(c2, 129ul)) {
      if (c3 >= 166ul && c3 <= 169ul) {
        return true;
      }
      
    }
    
  }
  
  return false;
}


Char_t readUTF8Char_h1(string_t text, uint32_t pos) {
  uint8_t i;
  uint32_t textlen;
  uint8_t len;
  textlen = (uint32_t)(string_length(text));
  c = text[pos];
  if (isequal_uint8_t(c & 32ul, 0ul)) {
    len = 2u;
  }
  
  else if (isequal_uint8_t(c & 16ul, 0ul)) {
    len = 3u;
  }
  
  else if (isequal_uint8_t(c & 8ul, 0ul)) {
    len = 4u;
  }
  
  else {
    return Char((uint32_t)(pos), (uint8_t)(1u), (int8_t)(false));
  }
  
  if (uintadd32((uint32_t)(pos), (uint32_t)((uint32_t)(len))) > textlen) {
    return Char((uint32_t)(pos), (uint8_t)((uint8_t)(uintsub32((uint32_t)(textlen), (uint32_t)(pos)))), (int8_t)(false));
  }
  
  for (i = 1u; i < len; i = i + 1ul) {
    if (!isequal_uint8_t(text[uintadd32((uint32_t)(pos), (uint32_t)((uint32_t)(i)))] & 192ul, 128ul)) {
      return Char((uint32_t)(pos), (uint8_t)((uint8_t)(uintadd8((uint8_t)(i), 1ul))), (int8_t)(false));
    }
    
  }
  
  if (encodingIsOverlong(text, (uint32_t)(pos), (uint8_t)(len)) || isTrojanSourceChar(text, (uint32_t)(pos), (uint8_t)(len))) {
    return Char((uint32_t)(pos), (uint8_t)(len), (int8_t)(false));
  }
  
  return Char((uint32_t)(pos), (uint8_t)(len), (int8_t)(true));
}


uint8_t hexDigit(uint8_t c) {
  if (c >= 48u && c <= 57u) {
    return uintsub8((uint8_t)(c), (uint8_t)(48u));
  }
  
  if (c >= 97u && c <= 122u) {
    return uintadd8(uintsub8((uint8_t)(c), (uint8_t)(97u)), 10ul);
  }
  
  if (c >= 65u && c <= 90u) {
    return uintadd8(uintsub8((uint8_t)(c), (uint8_t)(65u)), 10ul);
  }
  
  GlobalStringWriter_reset();
  tostring_string_t("%s", "Invalid hex digit");
  raise_with_code("Status.InvalidArgument ", GlobalStringWriter_string());
}


uint8_t hexToChar(uint8_t hi, uint8_t lo) {
  return hexDigit(hi) << 4ul | hexDigit(lo);
}


bool_t isHexDigit(uint8_t c) {
  return c >= 48u && c <= 57u || c >= 97u && c <= 102u || c >= 65u && c <= 70u;
}


bool_t isDigit(uint8_t c) {
  return c >= 48u && c <= 57u;
}


bool_t isWhitespace(uint8_t c) {
  return isequal_uint8_t(c, 32u) || isequal_uint8_t(c, 9u) || isequal_uint8_t(c, 13u);
}


uint8_t upper(uint8_t c) {
  if (c >= 97u && c <= 122u) {
    return uintadd8(uintsub8((uint8_t)(c), (uint8_t)(97u)), (uint8_t)(65u));
  }
  
  return c;
}


uint8_t lower(uint8_t c) {
  if (c >= 65u && c <= 90u) {
    return uintadd8(uintsub8((uint8_t)(c), (uint8_t)(65u)), (uint8_t)(97u));
  }
  
  return c;
}


bool_t isAsciiAlpha(string_t text, Char_t char) {
  if (!isequal_uint8_t(char.len, 1ul)) {
    return false;
  }
  
  c = text[char.pos];
  return c >= 97u && c <= 122u || c >= 65u && c <= 90u;
}


bool_t isAscii(string_t text, uint32_t pos) {
  return text[pos] < 128ul;
}


Char_t getChar(string_t text, uint32_t pos) {
  if (pos >= (uint32_t)(string_length(text))) {
    return Char((uint32_t)(pos), (uint8_t)(0u), (int8_t)(false));
  }
  
  if (isAscii(text, (uint32_t)(pos))) {
    if (isValidAsciiInRuneFile_h2(text, (uint32_t)(pos))) {
      return Char((uint32_t)(pos), (uint8_t)(1u), (int8_t)(true));
    }
    
    return Char((uint32_t)(pos), (uint8_t)(1u), (int8_t)(false));
  }
  
  return readUTF8Char_h1(text, (uint32_t)(pos));
}


uint64_t hashValues(uint64_t val1, uint64_t val2) {
  return rotl64((val1 ^ val2 ^ 11936128518282651045ul) * 16045690981923772711ul, 23ul);
}


uint64_t hashInteger_u64(uint64_t value) {
  return hashValues(0ul, (uint64_t)(value));
}


uint64_t hashString(string_t value) {
  uint64_t i;
  uint64_t hash;
  hash = 0ul;
  for (i = 0ul; i < string_length(value); i = uintadd64(i, 1ul)) {
    hash = hashValues(hash, (uint64_t)(value[i]));
  }
  
  return hash;
}




int main(int argc, const char **argv) {
  uint8_array_array_t l_iterable;
  uint8_array_t l;
  uint8_t val;
  uint64_t l_index;
  string_t s;
  for (val = 0ul; val < 32u; val = val + 1ul) {
    c = getChar(chr_u8(val), (uint32_t)(0u));
    if (isequal_uint8_t(val, 10u) || isequal_uint8_t(val, 13u) || isequal_uint8_t(val, 9u)) {
      if (!c.valid) {
        raise("Assertion failed");
      }
      
    }
    
    else {
      if (!!c.valid) {
        raise("Assertion failed");
      }
      
    }
    
  }
  
  for (val = 32u; val < 127u; val = val + 1ul) {
    c = getChar(chr_u8(val), (uint32_t)(0u));
    if (!c.valid) {
      raise("Assertion failed");
    }
    
  }
  
  c = getChar(chr_u8(127u), (uint32_t)(0u));
  if (!!c.valid) {
    raise("Assertion failed");
  }
  
  char = getChar("â‚¬", (uint32_t)(0u));
  if (!(isequal_uint32_t(char.pos, 0ul) && isequal_uint8_t(char.len, 3ul))) {
    raise("Assertion failed");
  }
  
  sch_u195_u182n = string_dup("á¼ˆÏ†ÏÎ¿Î´Î¯Ï„Î·");
  GlobalStringWriter_reset();
  tostring_string_t("%s", sch_u195_u182n);
  GlobalStringWriter_write("\n");
  printf("%s", GlobalStringWriter_string());
  pos = 0u;
  while (pos < (uint32_t)(string_length(sch_u195_u182n))) {
    char = getChar(sch_u195_u182n, (uint32_t)(pos));
    if (!char.valid) {
      raise("Assertion failed");
    }
    
    GlobalStringWriter_reset();
    tostring_string_t("%s", "'");
    tostring_string_t("%s", string_slice(sch_u195_u182n, (uint32_t)(pos), uintadd32((uint32_t)(pos), (uint32_t)((uint32_t)(char.len)))));
    tostring_string_t("%s", "'");
    GlobalStringWriter_write("\n");
    printf("%s", GlobalStringWriter_string());
    pos = uintadd32((uint32_t)(pos), (uint32_t)((uint32_t)(char.len)));
  }
  
  char = getChar("À€", (uint32_t)(0u));
  if (!!char.valid) {
    raise("Assertion failed");
  }
  
  char = getChar("à€€", (uint32_t)(0u));
  if (!!char.valid) {
    raise("Assertion failed");
  }
  
  char = getChar("ð‚‚¬", (uint32_t)(0u));
  if (!!char.valid) {
    raise("Assertion failed");
  }
  
  table = uint8_array_array_init(9ul, uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(128u), (uint8_t)(170u)), uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(128u), (uint8_t)(171u)), uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(128u), (uint8_t)(172u)), uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(128u), (uint8_t)(173u)), uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(128u), (uint8_t)(174u)), uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(129u), (uint8_t)(166u)), uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(129u), (uint8_t)(167u)), uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(129u), (uint8_t)(168u)), uint8_array_init(3ul, (uint8_t)(226u), (uint8_t)(129u), (uint8_t)(169u)));
  l_iterable = table;
  for (l_index = 0ul; l_index < array_length(l_iterable); l_index = l_index + 1ul) {
    l = l_iterable[l_index];
    s = (string_t)(l);
    char = getChar(s, (uint32_t)(0u));
    if (!!char.valid) {
      raise("Assertion failed");
    }
    
  }
  
  if (!(isequal_uint8_t(upper(97u), 65u))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(upper(110u), 78u))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(upper(122u), 90u))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(upper(32u), 32u))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(lower(65u), 97u))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(lower(78u), 110u))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(lower(90u), 122u))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(lower(32u), 32u))) {
    raise("Assertion failed");
  }
  
  if (!(!isDigit(97u) && !isDigit(65u))) {
    raise("Assertion failed");
  }
  
  if (!(!isDigit(103u) && !isDigit(71u))) {
    raise("Assertion failed");
  }
  
  if (!(isDigit(48u) && isDigit(57u))) {
    raise("Assertion failed");
  }
  
  if (!(!isDigit(uintsub8((uint8_t)(48u), (uint8_t)(1u))) && !isDigit(uintadd8((uint8_t)(57u), (uint8_t)(1u))))) {
    raise("Assertion failed");
  }
  
  if (!(isHexDigit(97u) && isHexDigit(65u))) {
    raise("Assertion failed");
  }
  
  if (!(!isHexDigit(103u) && !isHexDigit(71u))) {
    raise("Assertion failed");
  }
  
  if (!(isHexDigit(48u) && isHexDigit(57u))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(hexToChar((uint8_t)(99u), (uint8_t)(53u)), 197ul))) {
    raise("Assertion failed");
  }
  
  if (!(isequal_uint8_t(hexDigit(97u), 10ul) && isequal_uint8_t(hexDigit(65u), 10ul))) {
    raise("Assertion failed");
  }
  
  if (!!isWhitespace(27u)) {
    raise("Assertion failed");
  }
  
  if (!!isWhitespace(0u)) {
    raise("Assertion failed");
  }
  
  if (!!isWhitespace(95u)) {
    raise("Assertion failed");
  }
  
  if (!isWhitespace(32u)) {
    raise("Assertion failed");
  }
  
  if (!!isWhitespace(10u)) {
    raise("Assertion failed");
  }
  
  if (!isWhitespace(13u)) {
    raise("Assertion failed");
  }
  
  if (!isWhitespace(9u)) {
    raise("Assertion failed");
  }
  
  return 0;
}
