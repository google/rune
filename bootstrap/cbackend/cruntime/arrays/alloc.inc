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
