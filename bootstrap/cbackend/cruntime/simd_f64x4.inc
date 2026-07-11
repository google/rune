/*
 * Portable foundation for Rune's opaque four-lane f64 SIMD helpers.
 *
 * Handles intentionally point at scalar lane storage rather than carrying a
 * compiler vector type.  Generated Rune functions therefore never pass AVX
 * registers across an ABI boundary.  An AVX-specialized implementation can
 * replace these operation bodies later without changing the language-facing
 * builtins or their C signatures.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define RN_F64X4_LANES 4u

typedef struct rn_f64x4 {
  double lane[RN_F64X4_LANES];
} rn_f64x4;

typedef rn_f64x4 *rn_f64x4_t;

static inline void rn_f64x4_require(rn_f64x4_t value) {
  if (value == NULL) {
    raise("F64x4Null");
  }
}

static inline void rn_f64x4_require_range(const double *values,
                                          uint64_t offset) {
  if (values == NULL) {
    raise("F64x4ArrayNull");
  }
  const array_t *array = ((const array_t *)values) - 1;
  if (array->magic_hdr != ARRAY_MAGIC || array->magic_ftr != ARRAY_MAGIC ||
      array->element_size != sizeof(double) || offset > array->used ||
      array->used - offset < RN_F64X4_LANES) {
    raise("F64x4ArrayRange");
  }
}

static inline rn_f64x4_t f64x4New(void) {
  rn_f64x4_t result = (rn_f64x4_t)malloc(sizeof(rn_f64x4));
  if (result == NULL) {
    raise("OutOfMemory");
  }
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    result->lane[lane] = 0.0;
  }
  return result;
}

static inline void f64x4Set(rn_f64x4_t destination,
                            const rn_f64x4_t source) {
  rn_f64x4_require(destination);
  rn_f64x4_require(source);
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = source->lane[lane];
  }
}

static inline void f64x4Splat(rn_f64x4_t destination, double value) {
  rn_f64x4_require(destination);
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = value;
  }
}

static inline void f64x4Load(rn_f64x4_t destination, const double *source,
                             uint64_t offset) {
  rn_f64x4_require(destination);
  rn_f64x4_require_range(source, offset);
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = source[offset + lane];
  }
}

static inline void f64x4Store(const rn_f64x4_t source, double *destination,
                              uint64_t offset) {
  rn_f64x4_require(source);
  rn_f64x4_require_range(destination, offset);
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination[offset + lane] = source->lane[lane];
  }
}

static inline void f64x4Add(rn_f64x4_t destination, const rn_f64x4_t left,
                            const rn_f64x4_t right) {
  rn_f64x4_require(destination);
  rn_f64x4_require(left);
  rn_f64x4_require(right);
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = left->lane[lane] + right->lane[lane];
  }
}

static inline void f64x4Sub(rn_f64x4_t destination, const rn_f64x4_t left,
                            const rn_f64x4_t right) {
  rn_f64x4_require(destination);
  rn_f64x4_require(left);
  rn_f64x4_require(right);
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = left->lane[lane] - right->lane[lane];
  }
}

static inline void f64x4Mul(rn_f64x4_t destination, const rn_f64x4_t left,
                            const rn_f64x4_t right) {
  rn_f64x4_require(destination);
  rn_f64x4_require(left);
  rn_f64x4_require(right);
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = left->lane[lane] * right->lane[lane];
  }
}

static inline double f64x4HorizontalSum(const rn_f64x4_t value) {
  rn_f64x4_require(value);
  double sum = 0.0;
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    sum += value->lane[lane];
  }
  return sum;
}

/* Explicitly named approximate reciprocal square root.  The portable
 * fallback uses the correctly rounded scalar operation until AVX rsqrt
 * support is added behind this same API. */
static inline void f64x4ApproxReciprocalSqrt(rn_f64x4_t destination,
                                             const rn_f64x4_t source) {
  rn_f64x4_require(destination);
  rn_f64x4_require(source);
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = 1.0 / sqrt(source->lane[lane]);
  }
}

/* No AVX code is emitted by this scalar foundation. */
static inline uint8_t simdAvxAvailable(void) {
  return 0;
}
