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








/**
 * @brief Resize a rune array to exactly n elements (the value-level
 * resize() method).  Growth zero-fills the new elements.
 */
static inline void *array_setsize(void *arr, uint64_t n) {
  array_t *a = ((array_t *)arr) - 1;
  assert(a->magic_hdr == ARRAY_MAGIC);
  size_t old = a->used;
  a = array_resize(a, n);
  if (n > old) {
    memset((char *)(a + 1) + old * a->element_size, 0,
           ((size_t)n - old) * a->element_size);
  }
  a->used = n;
  return a + 1;
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

static inline uint64_t uintadd64(uint64_t a, uint64_t b) {
  return (uint64_t)uintadd(a, b, 64);
}

static inline uint32_t uintadd32(uint32_t a, uint32_t b) {
  return (uint32_t)uintadd(a, b, 32);
}

static inline uint64_t uintsub64(uint64_t a, uint64_t b) {
  return (uint64_t)uintsub(a, b, 64);
}

static inline uint64_t rotl64(uint64_t value, int distance) {
  return (value << distance) | (value >> (64 - distance));
}

typedef struct Keytab_t Keytab_t;
void tostring_Keytab_t(Keytab_t *self);
typedef struct Keyword_Keytab_string_t Keyword_Keytab_string_t;
void tostring_Keyword_Keytab_string_t(Keyword_Keytab_string_t *self);
typedef struct Sym_string_t Sym_string_t;
void tostring_Sym_string_t(Sym_string_t *self);
typedef Sym_string_t * *Sym_string_array_t;

Sym_string_t * *Sym_string_array_init(size_t num_initializers, ...) {
  va_list ap;
  size_t i;
  
  Sym_string_t * *a = (Sym_string_t **)array_alloc(sizeof(Sym_string_t *), num_initializers);
  va_start(ap, num_initializers);
  for (i = 0; i < num_initializers; ++i) {
    a[i] = (Sym_string_t *)va_arg(ap, Sym_string_t *);
  }
  return a;
}

void tostring_Sym_string_array_t(Sym_string_t * a[]) {
  array_t *array = ((array_t*)a) - 1;
  size_t i;
  if (a == 0) { tostring_string_t("[]"); return; }
  tostring_string_t("[");
  for(i = 0; i < array->used; ++i) {
    if (i > 0) tostring_string_t(", ");
    tostring_Sym_string_t(a[i]);
  }
  tostring_string_t("]");
}

bool isequal_Sym_string_array_t(Sym_string_array_t a1, Sym_string_array_t a2) {
  array_t *s1a = ((array_t*)a1) - 1;
  array_t *s2a = ((array_t*)a2) - 1;
  size_t i;
  
  if (s1a->used != s2a->used) return false;
  for (i = 0; i < s1a->used; ++i) {
    if (a1[i] != a2[i]) return false;
  }
  return true;
}

Sym_string_array_t append_Sym_string_array_t(Sym_string_array_t a, Sym_string_t * e) {
  array_t *array = ((array_t*)a) - 1;
  array = array_resize(array, array->used + 1);
  a = (Sym_string_t * *)(array + 1);
  a[array->used] = e;
  array->used += 1;
  return a;
}

typedef struct Symtab_t {
  uint32_t rn_id;
  Sym_string_array_t symTable;
  uint64_t numSyms;
} Symtab_t;
static uint32_t Symtab_idcounter = 0;

void tostring_Symtab_t(Symtab_t *self) {
  if (self == 0) { tostring_string_t("null"); return; }
  tostring_string_t("{");
  tostring_string_t("}");
  
}


struct Sym_string_t {
  uint32_t rn_id;
  Symtab_t * symtab;
  Sym_string_t * nextHashedSymtabSym;
  string_t name;
  uint64_t hashVal;
};
static uint32_t Sym_string_idcounter = 0;

void tostring_Sym_string_t(Sym_string_t *self) {
  if (self == 0) { tostring_string_t("null"); return; }
  tostring_string_t("{");
  tostring_string_t("name = ");
  tostring_string_t(self->name);
  tostring_string_t(", ");
  tostring_string_t("hashVal = ");
  tostring_uint64_t(self->hashVal);
  tostring_string_t("}");
  
}


struct Keyword_Keytab_string_t {
  uint32_t rn_id;
  Keytab_t * keytab;
  Keyword_Keytab_string_t * nextHashedKeytabKeyword;
  Sym_string_t * sym;
  uint32_t num;
};
static uint32_t Keyword_Keytab_string_idcounter = 0;

void tostring_Keyword_Keytab_string_t(Keyword_Keytab_string_t *self) {
  if (self == 0) { tostring_string_t("null"); return; }
  tostring_string_t("{");
  tostring_string_t("sym = ");
  tostring_Sym_string_t(self->sym);
  tostring_string_t(", ");
  tostring_string_t("num = ");
  tostring_uint32_t(self->num);
  tostring_string_t("}");
  
}


typedef Keyword_Keytab_string_t * *Keyword_Keytab_string_array_t;

Keyword_Keytab_string_t * *Keyword_Keytab_string_array_init(size_t num_initializers, ...) {
  va_list ap;
  size_t i;
  
  Keyword_Keytab_string_t * *a = (Keyword_Keytab_string_t **)array_alloc(sizeof(Keyword_Keytab_string_t *), num_initializers);
  va_start(ap, num_initializers);
  for (i = 0; i < num_initializers; ++i) {
    a[i] = (Keyword_Keytab_string_t *)va_arg(ap, Keyword_Keytab_string_t *);
  }
  return a;
}

void tostring_Keyword_Keytab_string_array_t(Keyword_Keytab_string_t * a[]) {
  array_t *array = ((array_t*)a) - 1;
  size_t i;
  if (a == 0) { tostring_string_t("[]"); return; }
  tostring_string_t("[");
  for(i = 0; i < array->used; ++i) {
    if (i > 0) tostring_string_t(", ");
    tostring_Keyword_Keytab_string_t(a[i]);
  }
  tostring_string_t("]");
}

bool isequal_Keyword_Keytab_string_array_t(Keyword_Keytab_string_array_t a1, Keyword_Keytab_string_array_t a2) {
  array_t *s1a = ((array_t*)a1) - 1;
  array_t *s2a = ((array_t*)a2) - 1;
  size_t i;
  
  if (s1a->used != s2a->used) return false;
  for (i = 0; i < s1a->used; ++i) {
    if (a1[i] != a2[i]) return false;
  }
  return true;
}

Keyword_Keytab_string_array_t append_Keyword_Keytab_string_array_t(Keyword_Keytab_string_array_t a, Keyword_Keytab_string_t * e) {
  array_t *array = ((array_t*)a) - 1;
  array = array_resize(array, array->used + 1);
  a = (Keyword_Keytab_string_t * *)(array + 1);
  a[array->used] = e;
  array->used += 1;
  return a;
}

struct Keytab_t {
  uint32_t rn_id;
  Keyword_Keytab_string_array_t keywordTable;
  uint64_t numKeywords;
};
static uint32_t Keytab_idcounter = 0;

void tostring_Keytab_t(Keytab_t *self) {
  if (self == 0) { tostring_string_t("null"); return; }
  tostring_string_t("{");
  tostring_string_t("}");
  
}


static Keytab_t * keytab;

uint64_t hashValue_Sym(Sym_string_t * value) {
  return (uint64_t)((uint32_t)(value));
}


void updateHashTableAfterResize_h2(Keytab_t * self_h2) {
  uint64_t i;
  uint64_t oldHash;
  Keyword_Keytab_string_t * prevEntry = 0;
  Keyword_Keytab_string_t * oldEntry = 0;
  uint64_t oldMask;
  Keyword_Keytab_string_t * nextEntry = 0;
  uint64_t newMask;
  uint64_t oldLength;
  uint64_t newHash;
  uint64_t rawHash;
  uint64_t newLength;
  newLength = array_length(self_h2->keywordTable);
  oldLength = newLength >> 1ul;
  newMask = uintsub64(newLength, 1ul);
  oldMask = uintsub64(oldLength, 1ul);
  for (i = 0ul; i < oldLength; i = i + 1ul) {
    oldEntry = self_h2->keywordTable[i];
    prevEntry = (Keyword_Keytab_string_t *)(0ul);
    while (!!oldEntry) {
      rawHash = hashValue_Sym(oldEntry->sym);
      newHash = rawHash & newMask;
      oldHash = rawHash & oldMask;
      if (!(isequal_uint64_t(oldHash, i))) {
        raise("Assertion failed");
      }
      
      nextEntry = oldEntry->nextHashedKeytabKeyword;
      if (isequal_uint64_t(newHash, oldHash)) {
        prevEntry = oldEntry;
      }
      
      else {
        if (!prevEntry) {
          self_h2->keywordTable[i] = nextEntry;
        }
        
        else {
          prevEntry->nextHashedKeytabKeyword = nextEntry;
        }
        
        oldEntry->nextHashedKeytabKeyword = self_h2->keywordTable[newHash];
        self_h2->keywordTable[newHash] = oldEntry;
      }
      
      oldEntry = nextEntry;
    }
    
  }
  
}


uint64_t hashValues(uint64_t val1, uint64_t val2) {
  return rotl64((val1 ^ val2 ^ 11936128518282651045ul) * 16045690981923772711ul, 23ul);
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


uint64_t hashValue_string(string_t value) {
  return hashString(value);
}


Sym_string_t * Sym_string(string_t name) {
  Sym_string_t * self = 0;
  self = calloc((uint32_t)(1u), sizeof(Sym_string_t));
  Sym_string_idcounter = Sym_string_idcounter + 1u;
  self->rn_id = Sym_string_idcounter;
  self->symtab = (Symtab_t *)(0ul);
  self->nextHashedSymtabSym = (Sym_string_t *)(0ul);
  self->name = name;
  self->hashVal = hashValue_string(name);
  return self;
}


static Symtab_t * theSymbolTable;

uint64_t Sym_string_hash(Sym_string_t * self) {
  return self->hashVal;
}


bool_t Sym_string_equals_Sym_string(Sym_string_t * self, Sym_string_t * other) {
  return isequal_string_t(self->name, other->name);
}


Sym_string_t * Symtab_findSym_Sym_string(Symtab_t * self, Sym_string_t * sym) {
  Sym_string_t * entry = 0;
  uint64_t hashVal;
  if (isequal_uint64_t(array_length(self->symTable), 0ul)) {
    return (Sym_string_t *)(0ul);
  }
  
  hashVal = Sym_string_hash(sym) & uintsub64(array_length(self->symTable), 1ul);
  entry = self->symTable[hashVal];
  while (!!entry) {
    if (Sym_string_equals_Sym_string(entry, sym)) {
      return entry;
    }
    
    entry = entry->nextHashedSymtabSym;
  }
  
  return (Sym_string_t *)(0ul);
}


void updateHashTableAfterResize_h1(Symtab_t * self_h1) {
  uint64_t i;
  uint64_t oldHash;
  Sym_string_t * prevEntry = 0;
  Sym_string_t * oldEntry = 0;
  uint64_t oldMask;
  Sym_string_t * nextEntry = 0;
  uint64_t newMask;
  uint64_t oldLength;
  uint64_t newHash;
  uint64_t rawHash;
  uint64_t newLength;
  newLength = array_length(self_h1->symTable);
  oldLength = newLength >> 1ul;
  newMask = uintsub64(newLength, 1ul);
  oldMask = uintsub64(oldLength, 1ul);
  for (i = 0ul; i < oldLength; i = i + 1ul) {
    oldEntry = self_h1->symTable[i];
    prevEntry = (Sym_string_t *)(0ul);
    while (!!oldEntry) {
      rawHash = Sym_string_hash(oldEntry);
      newHash = rawHash & newMask;
      oldHash = rawHash & oldMask;
      if (!(isequal_uint64_t(oldHash, i))) {
        raise("Assertion failed");
      }
      
      nextEntry = oldEntry->nextHashedSymtabSym;
      if (isequal_uint64_t(newHash, oldHash)) {
        prevEntry = oldEntry;
      }
      
      else {
        if (!prevEntry) {
          self_h1->symTable[i] = nextEntry;
        }
        
        else {
          prevEntry->nextHashedSymtabSym = nextEntry;
        }
        
        oldEntry->nextHashedSymtabSym = self_h1->symTable[newHash];
        self_h1->symTable[newHash] = oldEntry;
      }
      
      oldEntry = nextEntry;
    }
    
  }
  
}


void Symtab_insertSym_Sym_string(Symtab_t * self, Sym_string_t * entry) {
  uint64_t hash;
  if (isequal_uint64_t(self->numSyms, array_length(self->symTable))) {
    if (isequal_uint64_t(array_length(self->symTable), 0ul)) {
      self->symTable = array_setsize(self->symTable, 32ul);
    }
    
    else {
      self->symTable = array_setsize(self->symTable, array_length(self->symTable) << 1ul);
      updateHashTableAfterResize_h1(self);
    }
    
  }
  
  hash = Sym_string_hash(entry) & uintsub64(array_length(self->symTable), 1ul);
  entry->nextHashedSymtabSym = self->symTable[hash];
  self->symTable[hash] = entry;
  entry->symtab = self;
  self->numSyms = uintadd64(self->numSyms, 1ul);
}


Sym_string_t * string_new(string_t name) {
  Sym_string_t * oldSym = 0;
  Sym_string_t * newSym = 0;
  string_t nameCopy;
  nameCopy = name;
  newSym = Sym_string(nameCopy);
  oldSym = Symtab_findSym_Sym_string(theSymbolTable, newSym);
  if (!!oldSym) {
    return oldSym;
  }
  
  Symtab_insertSym_Sym_string(theSymbolTable, newSym);
  return newSym;
}


void Keytab_insertKeyword_Keyword_Keytab_string(Keytab_t * self, Keyword_Keytab_string_t * entry) {
  uint64_t hash;
  if (isequal_uint64_t(self->numKeywords, array_length(self->keywordTable))) {
    if (isequal_uint64_t(array_length(self->keywordTable), 0ul)) {
      self->keywordTable = array_setsize(self->keywordTable, 32ul);
    }
    
    else {
      self->keywordTable = array_setsize(self->keywordTable, array_length(self->keywordTable) << 1ul);
      updateHashTableAfterResize_h2(self);
    }
    
  }
  
  hash = hashValue_Sym(entry->sym) & uintsub64(array_length(self->keywordTable), 1ul);
  entry->nextHashedKeytabKeyword = self->keywordTable[hash];
  self->keywordTable[hash] = entry;
  entry->keytab = self;
  self->numKeywords = uintadd64(self->numKeywords, 1ul);
}


Keyword_Keytab_string_t * Keyword_Keytab_string(Keytab_t * keytab, string_t name) {
  Keyword_Keytab_string_t * self = 0;
  self = calloc((uint32_t)(1u), sizeof(Keyword_Keytab_string_t));
  Keyword_Keytab_string_idcounter = Keyword_Keytab_string_idcounter + 1u;
  self->rn_id = Keyword_Keytab_string_idcounter;
  self->keytab = (Keytab_t *)(0ul);
  self->nextHashedKeytabKeyword = (Keyword_Keytab_string_t *)(0ul);
  self->sym = string_new(name);
  self->num = 0u;
  Keytab_insertKeyword_Keyword_Keytab_string(keytab, self);
  return self;
}


void Keytab_checkKeywordTable(Keytab_t * self) {
  uint64_t i;
  uint64_t length;
  Keyword_Keytab_string_t * entry = 0;
  uint64_t hash;
  uint64_t mask;
  length = array_length(self->keywordTable);
  if (isequal_uint64_t(length, 0ul)) {
    return;
  }
  
  mask = uintsub64(length, 1ul);
  for (i = 0ul; i < length; i = i + 1ul) {
    entry = self->keywordTable[i];
    while (!!entry) {
      hash = hashValue_Sym(entry->sym) & mask;
      if (!isequal_uint64_t(i, hash)) {
        GlobalStringWriter_reset();
        tostring_string_t("%s", "Hash table check failed for ");
        tostring_Keyword_Keytab_string_t(entry);
        GlobalStringWriter_write("\n");
        printf("%s", GlobalStringWriter_string());
        GlobalStringWriter_reset();
        tostring_string_t("%s", "i = ");
        tostring_uint64_t(i);
        tostring_string_t("%s", ", hash = ");
        tostring_uint64_t(hash);
        raise_with_code("Exception.Internal ", GlobalStringWriter_string());
      }
      
      entry = entry->nextHashedKeytabKeyword;
    }
    
  }
  
}


uint32_t Keytab_setKeywordNums(Keytab_t * self) {
  Keyword_Keytab_string_t * keyword = 0;
  uint64_t _itr1_i;
  Keyword_Keytab_string_t * _itr1_entry = 0;
  Keytab_t * _itr1_self = 0;
  uint32_t num;
  num = 0u;
  _itr1_self = self;
  for (_itr1_i = 0ul; _itr1_i < array_length(_itr1_self->keywordTable); _itr1_i = _itr1_i + 1ul) {
    _itr1_entry = _itr1_self->keywordTable[_itr1_i];
    while (!!_itr1_entry) {
      keyword = _itr1_entry;
      keyword->num = num;
      num = uintadd32((uint32_t)(num), 1ul);
      _itr1_entry = _itr1_entry->nextHashedKeytabKeyword;
    }
    
  }
  
  return num;
}


Keyword_Keytab_string_t * Keytab_findKeyword_Sym_string(Keytab_t * self, Sym_string_t * key) {
  Keyword_Keytab_string_t * entry = 0;
  uint64_t hash;
  if (isequal_uint64_t(array_length(self->keywordTable), 0ul)) {
    return (Keyword_Keytab_string_t *)(0ul);
  }
  
  hash = hashValue_Sym(key) & uintsub64(array_length(self->keywordTable), 1ul);
  entry = self->keywordTable[hash];
  while (!!entry) {
    if ((key == entry->sym)) {
      return entry;
    }
    
    entry = entry->nextHashedKeytabKeyword;
  }
  
  return (Keyword_Keytab_string_t *)(0ul);
}


Keyword_Keytab_string_t * Keytab_new_string(Keytab_t * self, string_t name) {
  Sym_string_t * sym = 0;
  Keyword_Keytab_string_t * oldKeyword = 0;
  sym = string_new(name);
  oldKeyword = Keytab_findKeyword_Sym_string(self, sym);
  if (!!oldKeyword) {
    return oldKeyword;
  }
  
  return Keyword_Keytab_string(self, name);
}


Keyword_Keytab_string_t * Keytab_lookup_string(Keytab_t * self, string_t name) {
  return Keytab_findKeyword_Sym_string(self, string_new(name));
}


void Keytab_removeKeyword_Keyword_Keytab_string(Keytab_t * self, Keyword_Keytab_string_t * child) {
  Keyword_Keytab_string_t * entry = 0;
  uint64_t hash;
  Keyword_Keytab_string_t * prev = 0;
  hash = hashValue_Sym(child->sym) & uintsub64(array_length(self->keywordTable), 1ul);
  entry = self->keywordTable[hash];
  prev = (Keyword_Keytab_string_t *)(0ul);
  while (!!entry) {
    if ((entry == child)) {
      if (!prev) {
        self->keywordTable[hash] = child->nextHashedKeytabKeyword;
      }
      
      else {
        prev->nextHashedKeytabKeyword = child->nextHashedKeytabKeyword;
      }
      
      child->nextHashedKeytabKeyword = (Keyword_Keytab_string_t *)(0ul);
      child->keytab = (Keytab_t *)(0ul);
      self->numKeywords = uintsub64(self->numKeywords, 1ul);
      return;
    }
    
    prev = entry;
    entry = entry->nextHashedKeytabKeyword;
  }
  
  GlobalStringWriter_reset();
  tostring_string_t("%s", "Entry not found in map");
  raise_with_code("Exception.Internal ", GlobalStringWriter_string());
}


void Keyword_Keytab_string_destroy(Keyword_Keytab_string_t * self) {
  if (self) {
    if (self->rn_id) {
      if (!!self->keytab) {
        Keytab_removeKeyword_Keyword_Keytab_string(self->keytab, self);
      }
      
      self->rn_id = 0u;
    }
    
  }
  
}


void Keytab_destroy(Keytab_t * self) {
  Keyword_Keytab_string_t * nextKeywordEntry = 0;
  Keyword_Keytab_string_t * keywordEntry = 0;
  uint64_t xKeyword;
  if (self) {
    if (self->rn_id) {
      for (xKeyword = 0ul; xKeyword < array_length(self->keywordTable); xKeyword = xKeyword + 1ul) {
        keywordEntry = self->keywordTable[xKeyword];
        while (!!keywordEntry) {
          nextKeywordEntry = keywordEntry->nextHashedKeytabKeyword;
          keywordEntry->nextHashedKeytabKeyword = (Keyword_Keytab_string_t *)(0ul);
          keywordEntry->keytab = (Keytab_t *)(0ul);
          Keyword_Keytab_string_destroy(keywordEntry);
          keywordEntry = nextKeywordEntry;
        }
        
        self->keywordTable[xKeyword] = (Keyword_Keytab_string_t *)(0ul);
      }
      
      self->rn_id = 0u;
    }
    
  }
  
}


Keytab_t * Keytab() {
  Keytab_t * self = 0;
  self = calloc((uint32_t)(1u), sizeof(Keytab_t));
  Keytab_idcounter = Keytab_idcounter + 1u;
  self->rn_id = Keytab_idcounter;
  self->keywordTable = Keyword_Keytab_string_array_init(0ul);
  self->numKeywords = 0ul;
  return self;
}


uint64_t hashInteger_u64(uint64_t value) {
  return hashValues(0ul, (uint64_t)(value));
}


typedef struct tuple1_t {
} tuple1_t;

tuple1_t tuple1() {
  tuple1_t str;
  return str;
}

void tostring_tuple1_t(tuple1_t str) {
  tostring_string_t("(");
  tostring_string_t(")");
}

bool isequal_tuple1_t(tuple1_t s1, tuple1_t s2) {
  return true;
}

void Symtab_checkSymTable(Symtab_t * self) {
  uint64_t i;
  uint64_t length;
  Sym_string_t * entry = 0;
  uint64_t hash;
  uint64_t mask;
  length = array_length(self->symTable);
  if (isequal_uint64_t(length, 0ul)) {
    return;
  }
  
  mask = uintsub64(length, 1ul);
  for (i = 0ul; i < length; i = i + 1ul) {
    entry = self->symTable[i];
    while (!!entry) {
      hash = Sym_string_hash(entry) & mask;
      if (!isequal_uint64_t(i, hash)) {
        GlobalStringWriter_reset();
        tostring_string_t("%s", "Hash table check failed for ");
        tostring_Sym_string_t(entry);
        GlobalStringWriter_write("\n");
        printf("%s", GlobalStringWriter_string());
        GlobalStringWriter_reset();
        tostring_string_t("%s", "i = ");
        tostring_uint64_t(i);
        tostring_string_t("%s", ", hash = ");
        tostring_uint64_t(hash);
        raise_with_code("Exception.Internal ", GlobalStringWriter_string());
      }
      
      entry = entry->nextHashedSymtabSym;
    }
    
  }
  
}


void Symtab_removeSym_Sym_string(Symtab_t * self, Sym_string_t * child) {
  Sym_string_t * entry = 0;
  uint64_t hash;
  Sym_string_t * prev = 0;
  hash = Sym_string_hash(child) & uintsub64(array_length(self->symTable), 1ul);
  entry = self->symTable[hash];
  prev = (Sym_string_t *)(0ul);
  while (!!entry) {
    if ((entry == child)) {
      if (!prev) {
        self->symTable[hash] = child->nextHashedSymtabSym;
      }
      
      else {
        prev->nextHashedSymtabSym = child->nextHashedSymtabSym;
      }
      
      child->nextHashedSymtabSym = (Sym_string_t *)(0ul);
      child->symtab = (Symtab_t *)(0ul);
      self->numSyms = uintsub64(self->numSyms, 1ul);
      return;
    }
    
    prev = entry;
    entry = entry->nextHashedSymtabSym;
  }
  
  GlobalStringWriter_reset();
  tostring_string_t("%s", "Entry not found in map");
  raise_with_code("Exception.Internal ", GlobalStringWriter_string());
}


void Sym_string_destroy(Sym_string_t * self) {
  if (self) {
    if (self->rn_id) {
      if (!!self->symtab) {
        Symtab_removeSym_Sym_string(self->symtab, self);
      }
      
      self->rn_id = 0u;
    }
    
  }
  
}


void Symtab_destroy(Symtab_t * self) {
  Sym_string_t * nextSymEntry = 0;
  Sym_string_t * symEntry = 0;
  uint64_t xSym;
  if (self) {
    if (self->rn_id) {
      for (xSym = 0ul; xSym < array_length(self->symTable); xSym = xSym + 1ul) {
        symEntry = self->symTable[xSym];
        while (!!symEntry) {
          nextSymEntry = symEntry->nextHashedSymtabSym;
          symEntry->nextHashedSymtabSym = (Sym_string_t *)(0ul);
          symEntry->symtab = (Symtab_t *)(0ul);
          Sym_string_destroy(symEntry);
          symEntry = nextSymEntry;
        }
        
        self->symTable[xSym] = (Sym_string_t *)(0ul);
      }
      
      self->rn_id = 0u;
    }
    
  }
  
}


Symtab_t * Symtab() {
  Symtab_t * self = 0;
  self = calloc((uint32_t)(1u), sizeof(Symtab_t));
  Symtab_idcounter = Symtab_idcounter + 1u;
  self->rn_id = Symtab_idcounter;
  self->symTable = Sym_string_array_init(0ul);
  self->numSyms = 0ul;
  return self;
}




int main(int argc, const char **argv) {
  if (theSymbolTable) {
    Symtab_destroy(theSymbolTable);
  }
  
  theSymbolTable = Symtab(tuple1());
  if (false) {
    Sym_string("");
  }
  
  if (false) {
    keytab = Keytab(tuple1());
    Keyword_Keytab_string(keytab, ":");
  }
  
  if (keytab) {
    Keytab_destroy(keytab);
  }
  
  keytab = Keytab(tuple1());
  Keyword_Keytab_string(keytab, ":");
  Keyword_Keytab_string(keytab, "|");
  Keyword_Keytab_string(keytab, ";");
  if (!!Keytab_lookup_string(keytab, "\n")) {
    raise("Assertion failed");
  }
  
  if (!!!Keytab_lookup_string(keytab, ":")) {
    raise("Assertion failed");
  }
  
  if (!!!Keytab_lookup_string(keytab, "|")) {
    raise("Assertion failed");
  }
  
  if (!!!Keytab_lookup_string(keytab, ";")) {
    raise("Assertion failed");
  }
  
  GlobalStringWriter_reset();
  tostring_string_t("passed");
  GlobalStringWriter_write("\n");
  printf("%s", GlobalStringWriter_string());
  if (theSymbolTable) {
    Symtab_destroy(theSymbolTable);
  }
  
  if (keytab) {
    Keytab_destroy(keytab);
  }
  
  return 0;
}
