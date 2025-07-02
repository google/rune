#define RN_SIZET_MASK                   \
  (sizeof(size_t) == 4 ? (uint32_t)0x3u \
                       : (sizeof(size_t) == 8 ? (uint32_t)0x7 : 0xfffffffful))

#define RN_SIZET_SHIFT                \
  (sizeof(size_t) == 4 ? (uint32_t)2u \
                       : (sizeof(size_t) == 8 ? (uint32_t)0x3u : 0xfffffffful))


// Reverse an array byte by byte.
static inline void reverseBytes (uint8_t *data, size_t numElements, size_t elementSize) {
  uint8_t *first = data;
  uint8_t *last = data + (numElements - 1) * elementSize;
  while (first < last) {
    for (size_t i = 0; i < elementSize; i++) {
      uint8_t tmp = *first;
      *first++ = *last;
      *last++ = tmp;
    }
    last -= elementSize << 1;
  }
}

// Reverse the array by words.
static inline void reverseWords (size_t *data, size_t numElements, size_t elementWords) {
  size_t *first = data;
  size_t *last = data + (numElements - 1) * elementWords;
  while (first < last) {
    for (size_t i = 0; i < elementWords; i++) {
      size_t tmp = *first;
      *first++ = *last;
      *last++ = tmp;
    }
    last -= elementWords << 1;
  }
}

/**
 * @brief Reverse an array's elements in-place
 *
 * The ... args are used to swallow up generated artifacts in code generation. (i.e.,
 * the compiler will generate a specious empty tuple object because that's the parameter
 * passed into the reverse() method.
 *
 * @param ad - the array to reverse
 */
static inline void array_reverse (void *ad, ...) {
  array_t *a = ((array_t *)ad) - 1;

  assert(a->magic_hdr == ARRAY_MAGIC);
  assert(a->magic_ftr == ARRAY_MAGIC);

  if (a->used <= 1) return;

  if (a->element_size & RN_SIZET_MASK) {
    reverseBytes((uint8_t*)(ad), a->used, a->element_size);
  } else {
    reverseWords(ad, a->used, a->element_size >> RN_SIZET_SHIFT);
  }
}

