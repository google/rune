/**
 * @brief Append a new element to an array
 *
 * @param ad - the array to append to.
 * @param element - the new element to append to a.
 *
 * Note that we assume that `element` points to an element of the same
 * size as the elements of the array.
 */
static inline void *array_append (void *ad, void *element) {
  array_t *a = ((array_t *)ad) - 1;

  assert(a->magic == ARRAY_MAGIC);

  size_t new_used = array_checked_add(a->used, 1u);
  a = array_resize(a, new_used);
  ad = (void *)(a + 1);
  void *ptr = (void*)((uintptr_t)ad + a->used * a->element_size);
  memmove(ptr, element, a->element_size);
  a->used = new_used;
  return (void*)(a+1);
}
