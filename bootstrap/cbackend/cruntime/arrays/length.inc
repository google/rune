/**
 * @brief Return the length of an array
 *
 * @param dest - the array to extend
 *
 * The ... args are used to swallow up generated artifacts in code generation. (i.e.,
 * the compiler will generate a specious empty tuple object because that's the parameter
 * passed into the length() method.
 *
 * @returns the number of elements in the array
 */
static inline size_t array_length (void *a, ...) {
  array_t *array = ((array_t *)a) - 1;

  assert(array->magic_hdr == ARRAY_MAGIC);
  assert(array->magic_ftr == ARRAY_MAGIC);

  return array->used;
}