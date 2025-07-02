/*
 * Rune C backend primitive type qualities
 *
 * This is a chunk of code to inline into a rune-generated C files whenever
 * we want to generate equality functions for non-primitive types, such as
 * a tuple. The equality for an aggregate type will be built from the equality
 * methods for its constituent parts. The following methods define the
 * equalities for the primitive types.
 */

#include <string.h>

static inline bool_t isequal_bool_t(bool_t a, bool_t b) {
   return a == b;
}

static inline bool_t isequal_string_t(string_t a, string_t b) {
   return !strcmp(a, b);
}

static inline bool_t isequal_int8_t(int8_t a, int8_t b) {
   return a == b;
}

static inline bool_t isequal_int16_t(int16_t a, int16_t b) {
   return a == b;
}

static inline bool_t isequal_int32_t(int32_t a, int32_t b) {
   return a == b;
}

static inline bool_t isequal_int64_t(int64_t a, int64_t b) {
   return a == b;
}

static inline bool_t isequal_uint8_t(uint8_t a, uint8_t b) {
   return a == b;
}

static inline bool_t isequal_uint16_t(uint16_t a, uint16_t b) {
   return a == b;
}

static inline bool_t isequal_uint32_t(uint32_t a, uint32_t b) {
   return a == b;
}

static inline bool_t isequal_uint64_t(uint64_t a, uint64_t b) {
   return a == b;
}

static inline bool_t isequal_float_t(float a, float b) {
   return a == b;
}

static inline bool_t isequal_double_t(double a, double b) {
   return a == b;
}
