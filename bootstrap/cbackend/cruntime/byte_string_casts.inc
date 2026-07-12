/* Rune byte arrays and strings have different headed representations.
 * Cross-casts therefore materialize independent values; raw pointer casts
 * would read the wrong header and can run strlen beyond an array allocation. */
static inline char *string_from_u8_array(const uint8_t *bytes) {
  if (bytes == NULL) {
    return rn_stralloc(0u);
  }
  const array_t *array = ((const array_t *)bytes) - 1;
  assert(array->magic == ARRAY_MAGIC);
  assert(array->element_size == sizeof(uint8_t));
  assert(array->used <= array->capacity);
  char *result = rn_stralloc(array->used);
  if (array->used > 0u) {
    memcpy(result, bytes, array->used);
  }
  return result;
}

static inline uint8_t *u8_array_from_string(const char *value) {
  const size_t length = (size_t)string_length(value);
  uint8_t *result = (uint8_t *)array_alloc(sizeof(uint8_t), length);
  if (length > 0u) {
    memcpy(result, value, length);
  }
  return result;
}
