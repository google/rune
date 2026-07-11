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

// Return the squared pairwise horizontal sum of each input in the
// corresponding output lane. This matches the reduction and lane assembly
// used by the published n-body AVX implementation.
RN_F64X4_VALUE_FN rn_f64x4_t
f64x4SquaredLengths4(rn_f64x4_t input0, rn_f64x4_t input1,
                     rn_f64x4_t input2, rn_f64x4_t input3) {
  rn_f64x4_t result;
#if RN_F64X4_HAS_X86_AVX
  const __m256d value0 = _mm256_loadu_pd(input0.lane);
  const __m256d value1 = _mm256_loadu_pd(input1.lane);
  const __m256d value2 = _mm256_loadu_pd(input2.lane);
  const __m256d value3 = _mm256_loadu_pd(input3.lane);
  const __m256d squared0 = _mm256_mul_pd(value0, value0);
  const __m256d squared1 = _mm256_mul_pd(value1, value1);
  const __m256d squared2 = _mm256_mul_pd(value2, value2);
  const __m256d squared3 = _mm256_mul_pd(value3, value3);
  const __m256d pair01 = _mm256_hadd_pd(squared0, squared1);
  const __m256d pair23 = _mm256_hadd_pd(squared2, squared3);
  const __m256d highLow = _mm256_permute2f128_pd(pair01, pair23, 0x21);
  const __m256d lowHigh = _mm256_blend_pd(pair01, pair23, 0x0c);
  _mm256_storeu_pd(result.lane, _mm256_add_pd(highLow, lowHigh));
#else
  const rn_f64x4_t inputs[RN_F64X4_LANES] = {
      input0, input1, input2, input3};
  for (uint64_t input = 0; input < RN_F64X4_LANES; input++) {
    const double squared0 = inputs[input].lane[0] * inputs[input].lane[0];
    const double squared1 = inputs[input].lane[1] * inputs[input].lane[1];
    const double squared2 = inputs[input].lane[2] * inputs[input].lane[2];
    const double squared3 = inputs[input].lane[3] * inputs[input].lane[3];
    result.lane[input] = (squared0 + squared1) + (squared2 + squared3);
  }
#endif
  return result;
}

// Return the ordered <= comparison result with lane 0 in bit 0. Upper bits
// are always clear; in particular, NaN lanes compare false on every target.
RN_F64X4_VALUE_FN uint8_t f64x4LessEqualMask(rn_f64x4_t left,
                                              rn_f64x4_t right) {
#if RN_F64X4_HAS_X86_AVX
  const __m256d leftValue = _mm256_loadu_pd(left.lane);
  const __m256d rightValue = _mm256_loadu_pd(right.lane);
  const __m256d compared = _mm256_cmp_pd(leftValue, rightValue, _CMP_LE_OQ);
  return (uint8_t)(_mm256_movemask_pd(compared) & 0x0f);
#else
  uint8_t result = 0;
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    if (left.lane[lane] <= right.lane[lane]) {
      result |= (uint8_t)(1u << lane);
    }
  }
  return result;
#endif
}

RN_F64X4_VALUE_FN double f64x4Lane(rn_f64x4_t value, uint64_t lane) {
  if (lane >= RN_F64X4_LANES) {
    raise("F64x4LaneRange");
  }
  return value.lane[lane];
}

RN_F64X4_VALUE_FN double f64x4HorizontalSum(rn_f64x4_t value) {
  // Match the pairwise grouping used by the published n-body AVX reduction.
  // Clang can extract these lanes from the resident vector after inlining.
  const double low = value.lane[0] + value.lane[1];
  const double high = value.lane[2] + value.lane[3];
  return low + high;
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
  // Preserve C's left-associated `s * x * x` from the published leader.
  const __m256d squaredEstimate = _mm256_mul_pd(squared, estimate);
  const __m256d y = _mm256_mul_pd(squaredEstimate, estimate);
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
