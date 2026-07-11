/* Explicit, reusable buffered standard-input reader. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RN_BYTE_READER_MAGIC UINT64_C(0x52554e4542524452)
#define RN_BYTE_READER_BUFFER_CAPACITY ((size_t)65536)

#if defined(__GNUC__) || defined(__clang__)
#define RN_BYTE_READER_HOT inline __attribute__((always_inline))
#else
#define RN_BYTE_READER_HOT inline
#endif

typedef struct rn_byte_reader {
  uint64_t magic;
  FILE *stream;
  size_t position;
  size_t available;
  uint8_t reached_eof;
  uint8_t buffer[RN_BYTE_READER_BUFFER_CAPACITY];
} rn_byte_reader;

typedef rn_byte_reader *rn_byte_reader_t;

static RN_BYTE_READER_HOT void rn_byte_reader_require(
    const rn_byte_reader *reader) {
  if (reader == NULL || reader->magic != RN_BYTE_READER_MAGIC) {
    raise("ByteReaderInvalid");
  }
}

static inline rn_byte_reader_t rn_byte_reader_new(void) {
  rn_byte_reader *reader = (rn_byte_reader *)malloc(sizeof(rn_byte_reader));
  if (reader == NULL) {
    raise("OutOfMemory");
  }
  reader->stream = stdin;
  reader->position = 0;
  reader->available = 0;
  reader->reached_eof = 0;
  reader->magic = RN_BYTE_READER_MAGIC;
  return reader;
}

// Read one line into caller-owned Rune storage, stripping only the terminating
// LF. The last unterminated line is returned normally; a subsequent call
// returns the same allocation with used == 0. While a reader is live, its
// stream must not be consumed through a second API because it owns prefetch.
static RN_BYTE_READER_HOT uint8_t *rn_byte_reader_readln_into(
    rn_byte_reader_t reader, uint8_t *bytes) {
  rn_byte_reader_require(reader);
  if (bytes == NULL) {
    raise("ByteReaderInvalidArray");
  }
  array_t *array = ((array_t *)bytes) - 1;
  if (array->magic_hdr != ARRAY_MAGIC || array->magic_ftr != ARRAY_MAGIC ||
      array->element_size != sizeof(uint8_t) || array->capacity == 0 ||
      array->used > array->capacity) {
    raise("ByteReaderInvalidArray");
  }
  array->used = 0;

  for (;;) {
    if (reader->position == reader->available) {
      reader->position = 0;
      reader->available = 0;
      if (reader->reached_eof) {
        return bytes;
      }
      size_t count = fread(reader->buffer, 1,
                           RN_BYTE_READER_BUFFER_CAPACITY, reader->stream);
      if (count == 0) {
        if (ferror(reader->stream)) {
          raise("Error: could not read stdin");
        }
        reader->reached_eof = 1;
        return bytes;
      }
      reader->available = count;
    }

    const uint8_t *start = reader->buffer + reader->position;
    size_t remaining = reader->available - reader->position;
    const uint8_t *newline =
        (const uint8_t *)memchr(start, '\n', remaining);
    size_t segment = newline == NULL ? remaining : (size_t)(newline - start);
    if (segment > SIZE_MAX - array->used) {
      raise("ByteReaderLineOverflow");
    }
    size_t result_length = array->used + segment;
    if (result_length > array->capacity) {
      // array_resize doubles power-of-two capacities. Prove that both the
      // doubling and the eventual allocation size remain representable before
      // entering that generic helper.
      size_t checked_capacity = array->capacity;
      while (checked_capacity < result_length) {
        if (checked_capacity > (SIZE_MAX - sizeof(array_t)) / 2) {
          raise("ByteReaderLineOverflow");
        }
        checked_capacity *= 2;
      }
      array = array_resize(array, result_length);
      bytes = (uint8_t *)(array + 1);
    }
    if (segment != 0) {
      memcpy(bytes + array->used, start, segment);
    }
    array->used = result_length;
    reader->position += segment;

    if (newline != NULL) {
      ++reader->position;
      return bytes;
    }
  }
}

static inline void rn_byte_reader_free(rn_byte_reader_t reader) {
  if (reader == NULL) {
    return;
  }
  rn_byte_reader_require(reader);
  reader->magic = 0;
  reader->stream = NULL;
  reader->position = 0;
  reader->available = 0;
  reader->reached_eof = 1;
  free(reader);
}

#undef RN_BYTE_READER_BUFFER_CAPACITY
#undef RN_BYTE_READER_HOT
#undef RN_BYTE_READER_MAGIC
