/* Explicit, reusable buffered standard-input reader. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RN_BYTE_READER_MAGIC UINT64_C(0x52554e4542524452)
#define RN_BYTE_READER_BUFFER_CAPACITY ((size_t)65536)
#define RN_BYTE_READER_EOF ((uint16_t)256)

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

// Refill only after every previously buffered byte has been consumed. While a
// reader is live, its stream must not be consumed through a second API because
// the reader owns this prefetch.
static RN_BYTE_READER_HOT uint8_t rn_byte_reader_refill(
    rn_byte_reader_t reader) {
  if (reader->position != reader->available) {
    return 1;
  }
  reader->position = 0;
  reader->available = 0;
  if (reader->reached_eof) {
    return 0;
  }
  size_t count = fread(reader->buffer, 1,
                       RN_BYTE_READER_BUFFER_CAPACITY, reader->stream);
  if (count == 0) {
    if (ferror(reader->stream)) {
      raise("Error: could not read stdin");
    }
    reader->reached_eof = 1;
    return 0;
  }
  reader->available = count;
  return 1;
}

// Return the next byte without consuming it. u16 keeps all byte values
// distinct from the binary-safe EOF sentinel, 256.
static RN_BYTE_READER_HOT uint16_t rn_byte_reader_peek_byte(
    rn_byte_reader_t reader) {
  rn_byte_reader_require(reader);
  if (!rn_byte_reader_refill(reader)) {
    return RN_BYTE_READER_EOF;
  }
  return (uint16_t)reader->buffer[reader->position];
}

static RN_BYTE_READER_HOT array_t *rn_byte_reader_require_array(
    uint8_t *bytes) {
  if (bytes == NULL) {
    raise("ByteReaderInvalidArray");
  }
  array_t *array = ((array_t *)bytes) - 1;
  if (array->magic_hdr != ARRAY_MAGIC || array->magic_ftr != ARRAY_MAGIC ||
      array->element_size != sizeof(uint8_t) || array->capacity == 0 ||
      array->used > array->capacity) {
    raise("ByteReaderInvalidArray");
  }
  return array;
}

// Append bytes from the current reader position until delimiter or EOF while
// excluding every occurrence of skipped. The delimiter remains unconsumed so
// peek/read operations can distinguish it from EOF and decide how to consume
// it. Delimiter detection happens before filtering, so delimiter takes
// precedence when delimiter == skipped. Existing contents and capacity are
// retained, and embedded NUL bytes have ordinary byte semantics.
static RN_BYTE_READER_HOT uint8_t *
rn_byte_reader_append_until_byte_skipping_byte_into(
    rn_byte_reader_t reader, uint8_t *bytes, uint8_t delimiter,
    uint8_t skipped) {
  rn_byte_reader_require(reader);
  array_t *array = rn_byte_reader_require_array(bytes);

  for (;;) {
    if (!rn_byte_reader_refill(reader)) {
      return bytes;
    }

    const uint8_t *start = reader->buffer + reader->position;
    const size_t available = reader->available - reader->position;
    const uint8_t *delimiter_position =
        (const uint8_t *)memchr(start, delimiter, available);
    const size_t segment = delimiter_position == NULL ?
        available : (size_t)(delimiter_position - start);

    // The unfiltered segment length is a safe upper bound for this buffer.
    // Reserve it once before copying maximal retained runs, even when many
    // skipped bytes make the eventual result shorter.
    if (segment > SIZE_MAX - array->used) {
      raise("ByteReaderSpanOverflow");
    }
    const size_t result_upper_bound = array->used + segment;
    if (result_upper_bound > array->capacity) {
      size_t checked_capacity = array->capacity;
      while (checked_capacity < result_upper_bound) {
        if (checked_capacity > (SIZE_MAX - sizeof(array_t)) / 2) {
          raise("ByteReaderSpanOverflow");
        }
        checked_capacity *= 2;
      }
      array = array_resize(array, result_upper_bound);
      bytes = (uint8_t *)(array + 1);
    }

    uint8_t *output = bytes + array->used;
    const uint8_t *cursor = start;
    const uint8_t *end = start + segment;
    if (delimiter == skipped) {
      // No skipped byte can occur before the first delimiter in this segment.
      if (segment != 0) {
        memcpy(output, cursor, segment);
        output += segment;
      }
    } else {
      while (cursor < end) {
        const uint8_t *skipped_position =
            (const uint8_t *)memchr(cursor, skipped, (size_t)(end - cursor));
        const uint8_t *run_end = skipped_position == NULL ? end : skipped_position;
        const size_t run = (size_t)(run_end - cursor);
        if (run != 0) {
          memcpy(output, cursor, run);
          output += run;
        }
        if (skipped_position == NULL) {
          break;
        }
        cursor = skipped_position + 1;
      }
    }
    array->used = (size_t)(output - bytes);
    reader->position += segment;

    if (delimiter_position != NULL) {
      return bytes;
    }
  }
}

// Append one line to caller-owned Rune storage, stripping only the terminating
// LF. Existing contents and capacity are retained. The last unterminated line
// is appended normally; a subsequent call at EOF leaves the array unchanged.
static RN_BYTE_READER_HOT uint8_t *rn_byte_reader_appendln_into(
    rn_byte_reader_t reader, uint8_t *bytes) {
  rn_byte_reader_require(reader);
  array_t *array = rn_byte_reader_require_array(bytes);

  for (;;) {
    if (!rn_byte_reader_refill(reader)) {
      return bytes;
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

// Read one line into caller-owned Rune storage, preserving the original API's
// replacement semantics while sharing the append implementation above.
static RN_BYTE_READER_HOT uint8_t *rn_byte_reader_readln_into(
    rn_byte_reader_t reader, uint8_t *bytes) {
  rn_byte_reader_require(reader);
  array_t *array = rn_byte_reader_require_array(bytes);
  array->used = 0;
  return rn_byte_reader_appendln_into(reader, bytes);
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
#undef RN_BYTE_READER_EOF
#undef RN_BYTE_READER_HOT
#undef RN_BYTE_READER_MAGIC
