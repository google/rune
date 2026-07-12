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

  assert(da->magic == ARRAY_MAGIC);
  assert(sa->magic == ARRAY_MAGIC);
  assert(sa->element_size == da->element_size);
  array_validate_header(da);
  array_validate_header(sa);

  if (sa->used == 0) {
    return dest;
  }

  const int self_concat = dest == source;
  size_t old_used = da->used;
  size_t source_used = sa->used;
  size_t new_used = array_checked_add(old_used, source_used);
  size_t copy_size = array_payload_size(sa->element_size, source_used);
  da = array_resize(da, new_used);
  dest = (void*)(da + 1);
  if (self_concat) {
    source = dest;
  }
  void *ptr = (void*)((uintptr_t)dest + old_used * da->element_size);
  memmove(ptr, source, copy_size);
  da->used = new_used;
  return dest;
}
