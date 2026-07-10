/*
 * Rune C backend byte-level stdin/stdout builtins.
 *
 * Implements the Rune builtins readByte(), writeByte(), readBytes(),
 * writeBytes(), readlnInto() and appendBytes(). The helper names match the Rune
 * builtin names so calls can be emitted directly.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint8_t readByte(void) {
  int c = getchar();
  return c == EOF ? (uint8_t)0 : (uint8_t)c;
}

static inline void writeByte(uint8_t b) {
  putchar(b);
}

// Read one line from stdin, without the trailing newline.
static inline char *readln(void) {
  char *line = NULL;
  size_t cap = 0;
  ssize_t len = getline(&line, &cap, stdin);
  if (len < 0) {
    return rn_stralloc(0);
  }
  if (len > 0 && line[len - 1] == '\n') {
    --len;
  }
  char *result = rn_stralloc((size_t)len);
  memcpy(result, line, (size_t)len);
  free(line);
  return result;
}

// Reuse a caller-owned Rune byte array for one line from stdin. Like readln(),
// the returned array excludes the trailing newline and has used == 0 at EOF.
// Unlike readln(), the caller explicitly owns the mutable storage and receives
// its possibly reallocated data pointer back.
static inline uint8_t *readlnInto(uint8_t *bytes) {
  static char *line = NULL;
  static size_t capacity = 0;
  ssize_t len = getline(&line, &capacity, stdin);
  array_t *array = ((array_t *)bytes) - 1;
  if (len < 0) {
    if (ferror(stdin)) {
      raise("Error: could not read stdin");
    }
    array->used = 0;
    return bytes;
  }
  if (len > 0 && line[len - 1] == '\n') {
    --len;
  }
  if ((size_t)len > array->capacity) {
    array = array_resize(array, (size_t)len);
    bytes = (uint8_t *)(array + 1);
  }
  memcpy(bytes, line, (size_t)len);
  array->used = (size_t)len;
  return bytes;
}

// Append an exact source byte array to a caller-owned destination array. The
// destination's capacity is retained and grown only when required; both arrays
// may contain embedded NUL bytes.
static inline uint8_t *appendBytes(uint8_t *destination, const uint8_t *source) {
  array_t *destArray = ((array_t *)destination) - 1;
  const array_t *sourceArray = ((const array_t *)source) - 1;
  size_t sourceLength = sourceArray->used;
  if (sourceLength > SIZE_MAX - destArray->used) {
    raise("Error: appendBytes result exceeds array size");
  }
  size_t resultLength = destArray->used + sourceLength;
  if (resultLength > destArray->capacity) {
    destArray = array_resize(destArray, resultLength);
    destination = (uint8_t *)(destArray + 1);
  }
  memcpy(destination + destArray->used, source, sourceLength);
  destArray->used = resultLength;
  return destination;
}

// Read up to |n| bytes. The returned Rune array carries the actual byte count,
// so callers can distinguish a short read and preserve embedded NUL bytes.
static inline uint8_t *readBytes(uint64_t n) {
  uint8_t *buf = (uint8_t *)array_alloc(sizeof(uint8_t), (size_t)n);
  array_t *array = ((array_t *)buf) - 1;
  size_t got = fread(buf, 1, (size_t)n, stdin);
  array->used = got;
  if (got < (size_t)n && ferror(stdin)) {
    raise("Error: could not read stdin");
  }
  return buf;
}

// Write an exact array range. A zero count means all remaining bytes from
// |offset|, matching the public Rune API. Never use strlen: byte arrays may
// contain embedded NUL values.
static inline void writeBytes(const uint8_t *bytes, uint64_t count, uint64_t offset) {
  const array_t *array = ((const array_t *)bytes) - 1;
  size_t used = array->used;
  if (offset > used) {
    raise("Error: writeBytes offset exceeds array length");
  }
  if (count == 0) {
    count = used - (size_t)offset;
  }
  if (count > used - (size_t)offset) {
    raise("Error: writeBytes range exceeds array length");
  }
  const uint8_t *cursor = bytes + offset;
  size_t remaining = (size_t)count;
  while (remaining > 0) {
    size_t wrote = fwrite(cursor, 1, remaining, stdout);
    if (wrote == 0) {
      raise("Error: could not write stdout");
    }
    cursor += wrote;
    remaining -= wrote;
  }
}
