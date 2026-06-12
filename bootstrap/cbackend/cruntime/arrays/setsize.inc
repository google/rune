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
