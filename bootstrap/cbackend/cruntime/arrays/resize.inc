/**
 * @brief resize a rune array
 *
 * @param a - the array to resize
 * @param count - the new size
 *
 * If count is 0, then this means we are emptying the
 * array. Otherwise, we need to find the lowest power-of-two that is
 * greater then (or equal to) the requested size. This approach means
 * that array extension operations are amortized linear time.
 */
static inline array_t *array_resize (array_t *a, size_t count) {
  array_validate_header(a);
  size_t max_capacity = array_max_capacity(a->element_size);
  if (count > max_capacity) {
    raise(ARRAY_SIZE_OVERFLOW_ERROR);
    return a;
  }
  if (count == 0) {
    a->used = 0;
    return a;
  }
  if (count <= a->capacity) {
    return a;
  }

  size_t new_capacity =
      array_grown_capacity(a->element_size, a->capacity, count);
  size_t allocation_size =
      array_allocation_size(a->element_size, new_capacity);

  array_t *resized = (array_t *)realloc(a, allocation_size);
  if (resized == NULL) {
    raise(ARRAY_REALLOCATION_ERROR);
    return a;
  }
  /* realloc failure preserves the original object. Do not publish the new
   * capacity until allocation has succeeded. */
  resized->capacity = new_capacity;

  return resized;
}
