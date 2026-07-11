/*
 * Portable four-lane f64 values with an explicitly opt-in AVX fast path.
 *
 * The language-facing representation is always an ordinary 32-byte C struct.
 * Compiler vector types exist only as locals inside AVX-targeted, always-inline
 * operation bodies.  The Rune typechecker keeps these values local to one
 * generated function, so clang can scalar-replace the struct and retain the
 * four lanes in a vector register without exposing an AVX ABI.
 */

#include <math.h>
#include <stdint.h>

#define RN_F64X4_LANES 4u

typedef struct rn_f64x4_value {
  double lane[RN_F64X4_LANES];
} rn_f64x4_t;

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define RN_F64X4_HAS_X86_AVX 1
#define RN_F64X4_AVX_TARGET __attribute__((target("avx")))
#else
#define RN_F64X4_HAS_X86_AVX 0
#define RN_F64X4_AVX_TARGET
#endif

#if defined(__clang__) || defined(__GNUC__)
#define RN_F64X4_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define RN_F64X4_ALWAYS_INLINE inline
#endif

#define RN_F64X4_VALUE_FN static RN_F64X4_AVX_TARGET RN_F64X4_ALWAYS_INLINE

static inline int rn_f64x4_avx_available(void) {
#if RN_F64X4_HAS_X86_AVX
  static int available = -1;
  if (available < 0) {
    __builtin_cpu_init();
    available = __builtin_cpu_supports("avx") ? 1 : 0;
  }
  return available;
#else
  return 0;
#endif
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

RN_F64X4_VALUE_FN rn_f64x4_t f64x4FromLanes(double lane0, double lane1,
                                             double lane2, double lane3) {
  rn_f64x4_t result;
#if RN_F64X4_HAS_X86_AVX
  const __m256d value = _mm256_set_pd(lane3, lane2, lane1, lane0);
  _mm256_storeu_pd(result.lane, value);
#else
  result.lane[0] = lane0;
  result.lane[1] = lane1;
  result.lane[2] = lane2;
  result.lane[3] = lane3;
#endif
  return result;
}

RN_F64X4_VALUE_FN rn_f64x4_t f64x4Splat(double value) {
  rn_f64x4_t result;
#if RN_F64X4_HAS_X86_AVX
  const __m256d lanes = _mm256_set1_pd(value);
  _mm256_storeu_pd(result.lane, lanes);
#else
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    result.lane[lane] = value;
  }
#endif
  return result;
}

RN_F64X4_VALUE_FN rn_f64x4_t f64x4Load(const double *source,
                                        uint64_t offset) {
  rn_f64x4_require_range(source, offset);
  rn_f64x4_t result;
#if RN_F64X4_HAS_X86_AVX
  const __m256d value = _mm256_loadu_pd(source + offset);
  _mm256_storeu_pd(result.lane, value);
#else
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    result.lane[lane] = source[offset + lane];
  }
#endif
  return result;
}

RN_F64X4_VALUE_FN void f64x4Store(rn_f64x4_t source, double *destination,
                                   uint64_t offset) {
  rn_f64x4_require_range(destination, offset);
#if RN_F64X4_HAS_X86_AVX
  const __m256d value = _mm256_loadu_pd(source.lane);
  _mm256_storeu_pd(destination + offset, value);
#else
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination[offset + lane] = source.lane[lane];
  }
#endif
}

RN_F64X4_VALUE_FN rn_f64x4_t f64x4Add(rn_f64x4_t left,
                                       rn_f64x4_t right) {
  rn_f64x4_t result;
#if RN_F64X4_HAS_X86_AVX
  const __m256d leftValue = _mm256_loadu_pd(left.lane);
  const __m256d rightValue = _mm256_loadu_pd(right.lane);
  _mm256_storeu_pd(result.lane, _mm256_add_pd(leftValue, rightValue));
#else
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    result.lane[lane] = left.lane[lane] + right.lane[lane];
  }
#endif
  return result;
}

RN_F64X4_VALUE_FN rn_f64x4_t f64x4Sub(rn_f64x4_t left,
                                       rn_f64x4_t right) {
  rn_f64x4_t result;
#if RN_F64X4_HAS_X86_AVX
  const __m256d leftValue = _mm256_loadu_pd(left.lane);
  const __m256d rightValue = _mm256_loadu_pd(right.lane);
  _mm256_storeu_pd(result.lane, _mm256_sub_pd(leftValue, rightValue));
#else
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    result.lane[lane] = left.lane[lane] - right.lane[lane];
  }
#endif
  return result;
}

RN_F64X4_VALUE_FN rn_f64x4_t f64x4Mul(rn_f64x4_t left,
                                       rn_f64x4_t right) {
  rn_f64x4_t result;
#if RN_F64X4_HAS_X86_AVX
  const __m256d leftValue = _mm256_loadu_pd(left.lane);
  const __m256d rightValue = _mm256_loadu_pd(right.lane);
  _mm256_storeu_pd(result.lane, _mm256_mul_pd(leftValue, rightValue));
#else
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    result.lane[lane] = left.lane[lane] * right.lane[lane];
  }
#endif
  return result;
}

RN_F64X4_VALUE_FN double f64x4Lane(rn_f64x4_t value, uint64_t lane) {
  if (lane >= RN_F64X4_LANES) {
    raise("F64x4LaneRange");
  }
  return value.lane[lane];
}

RN_F64X4_VALUE_FN double f64x4HorizontalSum(rn_f64x4_t value) {
  // Keep a deterministic scalar addition order.  Clang can extract the lanes
  // directly from the resident vector after the surrounding operations inline.
  double sum = value.lane[0];
  sum += value.lane[1];
  sum += value.lane[2];
  sum += value.lane[3];
  return sum;
}

/* Explicitly approximate: convert through float rsqrt and perform one
 * Goldschmidt refinement.  The multiplication order is the published n-body
 * leader's order and is observable after millions of integration steps. */
RN_F64X4_VALUE_FN rn_f64x4_t
f64x4ApproxReciprocalSqrt(rn_f64x4_t source) {
  rn_f64x4_t result;
#if RN_F64X4_HAS_X86_AVX
  const __m256d squared = _mm256_loadu_pd(source.lane);
  __m256d estimate = _mm256_cvtps_pd(_mm_rsqrt_ps(_mm256_cvtpd_ps(squared)));
  const __m256d y = _mm256_mul_pd(squared, _mm256_mul_pd(estimate, estimate));
  __m256d correction = _mm256_mul_pd(y, _mm256_set1_pd(0.375));
  correction = _mm256_mul_pd(correction, y);
  __m256d subtraction = _mm256_mul_pd(y, _mm256_set1_pd(1.25));
  subtraction = _mm256_sub_pd(subtraction, _mm256_set1_pd(1.875));
  correction = _mm256_sub_pd(correction, subtraction);
  estimate = _mm256_mul_pd(estimate, correction);
  _mm256_storeu_pd(result.lane, estimate);
#else
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    result.lane[lane] = 1.0 / sqrt(source.lane[lane]);
  }
#endif
  return result;
}

static inline uint8_t simdAvxAvailable(void) {
  return rn_f64x4_avx_available() ? 1 : 0;
}
