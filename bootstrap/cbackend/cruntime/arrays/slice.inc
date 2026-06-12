/**
 * @brief Copy a [lo:hi) slice of an array into a fresh array.
 *
 * Out-of-range bounds clamp to the array size, matching string_slice.
 */
static inline void *array_slice(void *arr, uint64_t lo, uint64_t hi) {
  array_t *a = ((array_t *)arr) - 1;
  assert(a->magic_hdr == ARRAY_MAGIC);
  assert(a->magic_ftr == ARRAY_MAGIC);
  if (lo > a->used) {
    lo = a->used;
  }
  if (hi > a->used) {
    hi = a->used;
  }
  if (hi < lo) {
    hi = lo;
  }
  void *r = array_alloc(a->element_size, hi - lo);
  memcpy(r, (char *)arr + lo * a->element_size, (hi - lo) * a->element_size);
  return r;
}
