/* Checked size and capacity arithmetic shared by all Rune array operations. */

static inline size_t array_checked_add(size_t left, size_t right) {
  if (right > SIZE_MAX - left) {
    raise(ARRAY_SIZE_OVERFLOW_ERROR);
    return 0u;
  }
  return left + right;
}

static inline size_t array_max_capacity(size_t element_size) {
  if (element_size == 0u) {
    raise(ARRAY_ZERO_ELEMENT_SIZE_ERROR);
    return 0u;
  }
  return (SIZE_MAX - sizeof(array_t)) / element_size;
}

static inline size_t array_payload_size(size_t element_size, size_t count) {
  size_t max_capacity = array_max_capacity(element_size);
  if (count > max_capacity) {
    raise(ARRAY_SIZE_OVERFLOW_ERROR);
    return 0u;
  }
  return count * element_size;
}

static inline size_t array_allocation_size(size_t element_size,
                                           size_t capacity) {
  size_t payload_size = array_payload_size(element_size, capacity);
  return array_checked_add(sizeof(array_t), payload_size);
}

/* Preserve the established allocation policy: an empty array starts at the
 * named minimum, while a nonempty initializer list rounds up from one. At the
 * representable boundary, growth saturates at max_capacity instead of
 * wrapping past SIZE_MAX. */
static inline size_t array_initial_capacity(size_t element_size,
                                            size_t initializers) {
  size_t max_capacity = array_max_capacity(element_size);
  if (initializers > max_capacity) {
    raise(ARRAY_SIZE_OVERFLOW_ERROR);
    return 0u;
  }

  if (initializers == 0u) {
    if (ARRAY_MIN_CAPACITY > max_capacity) {
      raise(ARRAY_SIZE_OVERFLOW_ERROR);
      return 0u;
    }
    return ARRAY_MIN_CAPACITY;
  }

  size_t capacity = 1u;
  while (capacity < initializers) {
    if (capacity > max_capacity - capacity) {
      capacity = max_capacity;
    } else {
      capacity += capacity;
    }
  }
  return capacity;
}

static inline size_t array_grown_capacity(size_t element_size,
                                          size_t current_capacity,
                                          size_t required_count) {
  size_t max_capacity = array_max_capacity(element_size);
  if (current_capacity == 0u || current_capacity > max_capacity) {
    raise(ARRAY_INVALID_HEADER_ERROR);
    return 0u;
  }
  if (required_count > max_capacity) {
    raise(ARRAY_SIZE_OVERFLOW_ERROR);
    return 0u;
  }

  size_t capacity = current_capacity;
  while (capacity < required_count) {
    if (capacity > max_capacity - capacity) {
      capacity = max_capacity;
    } else {
      capacity += capacity;
    }
  }
  return capacity;
}

static inline void array_validate_header(const array_t *array) {
  if (array->magic != ARRAY_MAGIC) {
    raise(ARRAY_INVALID_HEADER_ERROR);
    return;
  }
  size_t max_capacity = array_max_capacity(array->element_size);
  if (array->capacity == 0u || array->capacity > max_capacity ||
      array->used > array->capacity) {
    raise(ARRAY_INVALID_HEADER_ERROR);
  }
}
