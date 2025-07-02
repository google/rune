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
  if (count == 0) {
    a->used = 0;
    return a;
  }
  if (count < a->capacity) {
    return a;
  }

  /* Double the array size until we have enough. */
  size_t new_capacity = a->capacity;
  while (new_capacity < count) {
    new_capacity *= 2;
  }

  a->capacity = new_capacity;
  void *newmem = realloc(a, new_capacity * a->element_size + sizeof(array_t));
  if (newmem == NULL) {
    raise("Error: could not reallocate array");
  }

  return (array_t*)newmem;
}