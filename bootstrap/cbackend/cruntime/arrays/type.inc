/**
 * @brief Internal implemenatation of Rune C backend arrays.
 *
 * Note that we use the "struct hack" to allocate the array data
 * immediately after the header.  We pass the pointer to the first
 * element so that we can simply use normal C array indexing.
 */

#define ARRAY_MAGIC UINT64_C(0xA99A73A656658C59)

/* Keep allocation policy and failure diagnostics in one place so every array
 * entry point rejects the same malformed sizes. */
#define ARRAY_MIN_CAPACITY ((size_t)16u)
#define ARRAY_ZERO_ELEMENT_SIZE_ERROR \
  "Error: array element size must be nonzero"
#define ARRAY_SIZE_OVERFLOW_ERROR \
  "Error: array size exceeds representable memory"
#define ARRAY_INVALID_HEADER_ERROR \
  "Error: corrupt array metadata"
#define ARRAY_ALLOCATION_ERROR \
  "Error: could not allocate array"
#define ARRAY_REALLOCATION_ERROR \
  "Error: could not reallocate array"

typedef struct {
  size_t element_size;
  size_t capacity;
  size_t used;
  /* Diagnostic sentinel immediately before the payload. This compact layout
   * deliberately trades the former leading sentinel for a smaller, aligned
   * header while retaining payload-underrun and accidental-pointer checks.
   * It is not a security boundary. */
  uint64_t magic;
} array_t;

#if defined(__x86_64__)
_Static_assert(sizeof(size_t) == 8u, "x86-64 size_t must be 64 bits");
_Static_assert(sizeof(uint64_t) == 8u, "uint64_t must be 64 bits");
_Static_assert(offsetof(array_t, element_size) == 0u,
               "unexpected Rune array element-size offset");
_Static_assert(offsetof(array_t, capacity) == 8u,
               "unexpected Rune array capacity offset");
_Static_assert(offsetof(array_t, used) == 16u,
               "unexpected Rune array length offset");
_Static_assert(offsetof(array_t, magic) == 24u,
               "Rune array sentinel must immediately precede payload");
_Static_assert(sizeof(array_t) == 32u,
               "x86-64 Rune arrays require a compact 32-byte header");
_Static_assert(sizeof(array_t) % 16u == 0u,
               "x86-64 Rune array payloads require 16-byte alignment");
_Static_assert(_Alignof(max_align_t) >= 16u,
               "x86-64 malloc alignment must support 16-byte array payloads");
#endif
