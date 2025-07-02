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

  assert(a->magic_hdr == ARRAY_MAGIC);
  assert(a->magic_ftr == ARRAY_MAGIC);

  a = array_resize(a, a->used + 1);
  void *ptr = (void*)((uintptr_t)ad + a->used * a->element_size);
  memmove(ptr, element, a->element_size);
  a->used += 1;
  return (void*)(a+1);
}

