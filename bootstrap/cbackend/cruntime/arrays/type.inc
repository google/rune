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