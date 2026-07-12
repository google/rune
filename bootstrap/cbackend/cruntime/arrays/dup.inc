/**
 * @brief Copy an entire array into a fresh, independent array.
 *
 * Rune arrays have value semantics: assigning one array-typed variable to
 * another (q = p) must produce a distinct copy so writes through one name do
 * not mutate the other.  Generic over element type via the header's
 * element_size (mirrors array_slice(arr, 0, used)).
 */
static inline void *array_dup(void *arr) {
  array_t *a = ((array_t *)arr) - 1;
  assert(a->magic == ARRAY_MAGIC);
  void *r = array_alloc(a->element_size, a->used);
  memcpy(r, arr, a->used * a->element_size);
  return r;
}
