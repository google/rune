/**
 * @brief Concatenate two arrays
 *
 * @param dest - the array to extend
 * @param source - the array whose elements are to be copied
 *
 * TODO: the existing C compiler handles arrays of arrays
 * specially.
 */
static inline void *array_concat (void *dest, void *source) {
  array_t *da = ((array_t *)dest) - 1;
  array_t *sa = ((array_t *)source) - 1;

  assert(da->magic_hdr == ARRAY_MAGIC);
  assert(da->magic_ftr == ARRAY_MAGIC);
  assert(sa->magic_hdr == ARRAY_MAGIC);
  assert(sa->magic_ftr == ARRAY_MAGIC);
  assert(sa->element_size == da->element_size);

  if (sa->used == 0) {
    return dest;
  }

  da = array_resize(da, sa->used + da->used);
  dest = (void*)(da + 1);
  void *ptr = (void*)((uintptr_t)dest + da->used * da->element_size);
  memmove(ptr, source, sa->element_size * sa->used);
  da->used += sa->used;
  return dest;
}