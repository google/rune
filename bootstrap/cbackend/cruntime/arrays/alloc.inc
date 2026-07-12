/* Rune C array allocation and initialization
 */

static inline void *array_alloc (size_t element_size, size_t num_initializers) {
    size_t capacity = array_initial_capacity(element_size, num_initializers);
    size_t allocation_size = array_allocation_size(element_size, capacity);

    array_t *array = (array_t *)malloc(allocation_size);
    if (array == NULL) {
       raise(ARRAY_ALLOCATION_ERROR);
       return NULL;
    }
    /* set up array informational struct. */
    array->element_size = element_size;
    array->capacity = capacity;
    array->used = num_initializers;
    array->magic = ARRAY_MAGIC;
    return array + 1; /* skip the header */
}
