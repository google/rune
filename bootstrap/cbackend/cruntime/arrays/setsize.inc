/**
 * @brief Resize a rune array to exactly n elements (the value-level
 * resize() method).  Growth zero-fills the new elements.
 */
static inline void *array_setsize(void *arr, uint64_t n) {
  array_t *a = ((array_t *)arr) - 1;
  assert(a->magic == ARRAY_MAGIC);
  if (n > (uint64_t)SIZE_MAX) {
    raise(ARRAY_SIZE_OVERFLOW_ERROR);
    return arr;
  }
  size_t count = (size_t)n;
  size_t old = a->used;
  a = array_resize(a, count);
  if (count > old) {
    memset((char *)(a + 1) + old * a->element_size, 0,
           (count - old) * a->element_size);
  }
  a->used = count;
  return a + 1;
}
