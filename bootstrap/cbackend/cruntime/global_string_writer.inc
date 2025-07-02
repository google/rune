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

static inline void tostring_float_t(float f) {
  StringWriter_write(&globalStringWriter, "%." FLT_SIG_DIGITS "g", f);
}

static inline void tostring_double_t(double d) {
  StringWriter_write(&globalStringWriter, "%." DBL_SIG_DIGITS "g", d);
}
